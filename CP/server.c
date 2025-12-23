#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"

const int SHIPS[] = {4, 3, 3, 2, 2};
const int SHIP_COUNT = 5;

SharedState *state = NULL;
int shm_fd = -1;
sem_t *request_sem = NULL;
sem_t *slot_sems[MAX_PLAYERS];
sem_t *resp_sems[MAX_PLAYERS];
volatile sig_atomic_t running = 1;

void build_sem_name(const char *prefix, int idx, char *out, size_t len) {
    snprintf(out, len, "%s%d", prefix, idx);
}

void cleanup_ipc() {
    if (request_sem) {
        sem_close(request_sem);
        sem_unlink(SEM_REQUEST_NAME);
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        char name[64];
        if (slot_sems[i]) sem_close(slot_sems[i]);
        build_sem_name(SEM_SLOT_PREFIX, i, name, sizeof(name));
        sem_unlink(name);

        if (resp_sems[i]) sem_close(resp_sems[i]);
        build_sem_name(SEM_RESP_PREFIX, i, name, sizeof(name));
        sem_unlink(name);
    }

    if (state) {
        munmap(state, sizeof(SharedState));
        state = NULL;
    }
    if (shm_fd >= 0) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
        shm_fd = -1;
    }
}

void handle_signal(int sig) {
    (void)sig;
    running = 0;
    if (request_sem) sem_post(request_sem);
}

int setup_ipc() {
    shm_unlink(SHM_NAME);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(shm_fd, sizeof(SharedState)) < 0) {
        perror("ftruncate");
        return -1;
    }
    state = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(state, 0, sizeof(SharedState));

    sem_unlink(SEM_REQUEST_NAME);
    request_sem = sem_open(SEM_REQUEST_NAME, O_CREAT, 0666, 0);
    if (request_sem == SEM_FAILED) {
        perror("sem_open request");
        return -1;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        char name[64];
        build_sem_name(SEM_SLOT_PREFIX, i, name, sizeof(name));
        sem_unlink(name);
        slot_sems[i] = sem_open(name, O_CREAT, 0666, 1);
        if (slot_sems[i] == SEM_FAILED) {
            perror("sem_open slot");
            return -1;
        }
        build_sem_name(SEM_RESP_PREFIX, i, name, sizeof(name));
        sem_unlink(name);
        resp_sems[i] = sem_open(name, O_CREAT, 0666, 0);
        if (resp_sems[i] == SEM_FAILED) {
            perror("sem_open response");
            return -1;
        }
    }
    return 0;
}

int find_player_by_login(const char *login) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->players[i].active && strncmp(state->players[i].login, login, MAX_NAME) == 0) {
            return i;
        }
    }
    return -1;
}

int find_game_by_name(const char *name) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (state->games[i].active && strncmp(state->games[i].name, name, MAX_NAME) == 0) {
            return i;
        }
    }
    return -1;
}

int next_free_game() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!state->games[i].active) {
            return i;
        }
    }
    return -1;
}

void reset_player_slot(int slot) {
    memset(&state->players[slot], 0, sizeof(Player));
    state->players[slot].game_id = -1;
}

void reset_game(Game *g) {
    memset(g, 0, sizeof(Game));
    g->players[0] = g->players[1] = -1;
    g->turn = 0;
    g->winner_slot = -1;
}

void cleanup_invites_for_player(int slot) {
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *inv = &state->invites[i];
        if (inv->active && (inv->from_player == slot || inv->to_player == slot))
            memset(inv, 0, sizeof(Invite));
    }
}

void cleanup_invites_for_game(int game_id) {
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *inv = &state->invites[i];
        if (inv->active && inv->game_id == game_id)
            memset(inv, 0, sizeof(Invite));
    }
}

int seat_for_player(Game *g, int slot) {
    if (g->players[0] == slot) return 0;
    if (g->players[1] == slot) return 1;
    return -1;
}

int is_adjacent(CellState board[BOARD_SIZE][BOARD_SIZE], int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                if (board[ny][nx] == CELL_SHIP) return 1;
            }
        }
    }
    return 0;
}

void place_ships(CellState board[BOARD_SIZE][BOARD_SIZE], int *out_cells) {
    memset(board, 0, sizeof(CellState) * BOARD_SIZE * BOARD_SIZE);
    int total_cells = 0;
    for (int s = 0; s < SHIP_COUNT; s++) {
        int len = SHIPS[s];
        int placed = 0;
        for (int attempt = 0; attempt < 200 && !placed; attempt++) {
            int horizontal = rand() % 2;
            int x = rand() % BOARD_SIZE;
            int y = rand() % BOARD_SIZE;
            if (horizontal) {
                if (x + len > BOARD_SIZE) {
                    continue;
                }
                int fits = 1;
                for (int dx = 0; dx < len; dx++) {
                    if (board[y][x + dx] != CELL_EMPTY || is_adjacent(board, x + dx, y)) {
                        fits = 0;
                        break;
                    }
                }
                if (!fits) {
                    continue;
                }
                for (int dx = 0; dx < len; dx++) {
                    board[y][x + dx] = CELL_SHIP;
                }
            } else {
                if (y + len > BOARD_SIZE) {
                    continue;
                }
                int fits = 1;
                for (int dy = 0; dy < len; dy++) {
                    if (board[y + dy][x] != CELL_EMPTY || is_adjacent(board, x, y + dy)) {
                        fits = 0;
                        break;
                    }
                }
                if (!fits) {
                    continue;
                }
                for (int dy = 0; dy < len; dy++) {
                    board[y + dy][x] = CELL_SHIP;
                }
            }
            placed = 1;
            total_cells += len;
        }
        if (!placed) {
            s = -1;
            memset(board, 0, sizeof(CellState) * BOARD_SIZE * BOARD_SIZE);
            total_cells = 0;
        }
    }
    if (out_cells) {
        *out_cells = total_cells;
    }
}

void copy_enemy_view(Game *g, int seat, CellState dest[BOARD_SIZE][BOARD_SIZE]) {
    int enemy = seat ^ 1;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            CellState c = g->boards[enemy][y][x];
            if (c == CELL_HIT || c == CELL_MISS) {
                dest[y][x] = c;
            } else {
                dest[y][x] = CELL_EMPTY;
            }
        }
    }
}

void fill_boards(Game *g, int seat, Response *resp) {
    resp->has_boards = 1;
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            resp->your_board[y][x] = g->boards[seat][y][x];
        }
    }
    copy_enemy_view(g, seat, resp->enemy_board);
}

void handle_register(int slot, const Request *req, Response *resp) {
    if (state->players[slot].active) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Slot %d already in use.", slot);
        return;
    }
    if (req->text[0] == '\0') {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Login cannot be empty.");
        return;
    }
    if (find_player_by_login(req->text) != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Login '%s' already online.", req->text);
        return;
    }
    reset_player_slot(slot);
    state->players[slot].active = 1;
    strncpy(state->players[slot].login, req->text, MAX_NAME - 1);
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Добро пожаловать, %s!", req->text);
    printf("[REGISTER] [PLAYER slot=%d login=\"%s\"] connected.\n", slot, req->text);
}

void handle_list_games(Response *resp) {
    resp->code = 0;
    char buffer[MAX_MESSAGE];
    buffer[0] = '\0';
    for (int i = 0; i < MAX_GAMES; i++) {
        Game *g = &state->games[i];
        if (!g->active) {
            continue;
        }
        char line[80];
        int players_in_game = (g->players[0] != -1) + (g->players[1] != -1);
        snprintf(line, sizeof(line), "%s (%d/2 players)%s\n", g->name, players_in_game,
                 g->winner_slot != -1 ? " - finished" : "");
        if (strlen(buffer) + strlen(line) < sizeof(buffer)) {
            strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
        }
    }
    if (buffer[0] == '\0') {
        snprintf(resp->message, MAX_MESSAGE, "Нет активных игр.");
    } else {
        snprintf(resp->message, MAX_MESSAGE, "%s", buffer);
    }
}

void handle_create_game(int slot, const Request *req, Response *resp) {
    if (!state->players[slot].active) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Сначала войдите.");
        return;
    }
    if (state->players[slot].game_id != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Вы уже в игре.");
        return;
    }
    if (req->text[0] == '\0') {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Имя игры пустое.");
        return;
    }
    if (find_game_by_name(req->text) != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра с именем %s уже существует.", req->text);
        return;
    }
    int game_idx = next_free_game();
    if (game_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Нет места для новой игры.");
        return;
    }
    Game *g = &state->games[game_idx];
    reset_game(g);
    g->active = 1;
    strncpy(g->name, req->text, MAX_NAME - 1);
    g->players[0] = slot;
    g->ready[0] = 1;
    g->players[1] = -1;
    g->winner_slot = -1;
    place_ships(g->boards[0], &g->ships_left[0]);
    g->ships_left[1] = 0;
    g->turn = 0;
    state->players[slot].game_id = game_idx;
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Игра '%s' создана. Ждите соперника.", g->name);
    printf("[CREATE_GAME] [PLAYER slot=%d login=\"%s\"] created [GAME id=%d name=\"%s\"].\n",
        slot, state->players[slot].login, game_idx, g->name);
}

void handle_join_game(int slot, const Request *req, Response *resp) {
    if (!state->players[slot].active) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Сначала войдите.");
        return;
    }
    if (state->players[slot].game_id != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Вы уже в игре.");
        return;
    }
    int game_idx = find_game_by_name(req->text);
    if (game_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра не найдена.");
        return;
    }
    Game *g = &state->games[game_idx];
    if (!g->active || g->players[1] != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра заполнена или недоступна.");
        return;
    }
    g->players[1] = slot;
    g->ready[1] = 1;
    place_ships(g->boards[1], &g->ships_left[1]);
    g->turn = rand() % 2;
    g->winner_slot = -1;
    state->players[slot].game_id = game_idx;
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Вы присоединились к игре '%s'.", g->name);
    printf("[JOIN_GAME] [PLAYER slot=%d login=\"%s\"] joined [GAME id=%d name=\"%s\"].\n",
        slot, state->players[slot].login, game_idx, g->name);
}

void handle_send_invite(int slot, const Request *req, Response *resp) {
    if (state->players[slot].game_id < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Создайте или войдите в игру перед приглашением.");
        return;
    }
    if (req->text[0] == '\0') {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Логин не задан.");
        return;
    }
    int target = find_player_by_login(req->text);
    if (target < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игрок %s не найден.", req->text);
        return;
    }
    if (target == slot) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Нельзя пригласить себя.");
        return;
    }
    if (state->players[target].game_id != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игрок уже в другой игре.");
        return;
    }
    Game *g = &state->games[state->players[slot].game_id];
    if (!g->active || g->players[1] != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра уже заполнена.");
        return;
    }
    for (int i = 0; i < MAX_INVITES; i++) {
        if (!state->invites[i].active) {
            state->invites[i].active = 1;
            state->invites[i].from_player = slot;
            state->invites[i].to_player = target;
            state->invites[i].game_id = state->players[slot].game_id;
            resp->code = 0;
            snprintf(resp->message, MAX_MESSAGE, "Приглашение отправлено игроку %s.", req->text);
            printf("[INVITE] [PLAYER slot=%d login=\"%s\"] invited [PLAYER slot=%d login=\"%s\"] to [GAME id=%d].\n",
                slot,
                state->players[slot].login,
                target,
                state->players[target].login,
                state->players[slot].game_id);
            return;
        }
    }
    resp->code = 1;
    snprintf(resp->message, MAX_MESSAGE, "Нет места для нового приглашения.");
}

void handle_list_invites(int slot, Response *resp) {
    char buffer[MAX_MESSAGE];
    buffer[0] = '\0';
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *inv = &state->invites[i];
        if (inv->active && inv->to_player == slot) {
            Game *g = &state->games[inv->game_id];
            char line[80];
            snprintf(line, sizeof(line), "От %s в игру '%s'\n",
                     state->players[inv->from_player].login, g->name);
            if (strlen(buffer) + strlen(line) < sizeof(buffer)) {
                strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
            }
        }
    }
    resp->code = 0;
    if (buffer[0] == '\0') {
        snprintf(resp->message, MAX_MESSAGE, "Приглашений нет.");
    } else {
        snprintf(resp->message, MAX_MESSAGE, "%s", buffer);
    }
}

void handle_decline_invite(int slot, const Request *req, Response *resp) {
    int removed = 0;
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *inv = &state->invites[i];
        if (!inv->active || inv->to_player != slot) {
            continue;
        }
        if (req->text[0] == '\0' ||
            strncmp(state->players[inv->from_player].login, req->text, MAX_NAME) == 0) {
            memset(inv, 0, sizeof(Invite));
            removed++;
        }
    }
    resp->code = 0;
    if (removed == 0) {
        snprintf(resp->message, MAX_MESSAGE, "Нечего отклонять.");
    } else {
        snprintf(resp->message, MAX_MESSAGE, "Отклонено приглашений: %d.", removed);
    }
}

void handle_accept_invite(int slot, const Request *req, Response *resp) {
    if (state->players[slot].game_id != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Вы уже в игре.");
        return;
    }
    int invite_idx = -1;
    for (int i = 0; i < MAX_INVITES; i++) {
        Invite *inv = &state->invites[i];
        if (!inv->active || inv->to_player != slot) {
            continue;
        }
        if (req->text[0] == '\0' ||
            strncmp(state->players[inv->from_player].login, req->text, MAX_NAME) == 0) {
            invite_idx = i;
            break;
        }
    }
    if (invite_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Приглашение не найдено.");
        return;
    }
    Invite *inv = &state->invites[invite_idx];
    Game *g = &state->games[inv->game_id];
    if (!g->active || g->players[1] != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра уже недоступна.");
        memset(inv, 0, sizeof(Invite));
        return;
    }
    g->players[1] = slot;
    g->ready[1] = 1;
    place_ships(g->boards[1], &g->ships_left[1]);
    g->turn = rand() % 2;
    g->winner_slot = -1;
    state->players[slot].game_id = inv->game_id;
    cleanup_invites_for_player(slot);
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Вы приняли приглашение в игру '%s'.", g->name);
    printf("[ACCEPT_INVITE] [PLAYER slot=%d login=\"%s\"] joined [GAME id=%d name=\"%s\"].\n",
        slot, state->players[slot].login, inv->game_id, g->name);
}

void handle_game_status(int slot, Response *resp) {
    int game_idx = state->players[slot].game_id;
    if (game_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Вы не в игре.");
        return;
    }
    Game *g = &state->games[game_idx];
    int seat = seat_for_player(g, slot);
    if (seat < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра повреждена.");
        return;
    }
    resp->code = 0;
    resp->game_id = game_idx;
    resp->winner_slot = g->winner_slot;
    resp->your_turn = (g->winner_slot == -1 && g->turn == seat);
    fill_boards(g, seat, resp);
    snprintf(resp->message, MAX_MESSAGE, "Игра '%s'. %s", g->name,
             resp->winner_slot == -1 ? "Игра продолжается." : "Игра завершена.");
}

void handle_quit_game(int slot, Response *resp) {
    int game_idx = state->players[slot].game_id;
    if (game_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Вы не в игре.");
        return;
    }
    Game *g = &state->games[game_idx];
    int seat = seat_for_player(g, slot);
    if (seat < 0) {
        state->players[slot].game_id = -1;
        resp->code = 0;
        snprintf(resp->message, MAX_MESSAGE, "Выход из игры.");
        return;
    }
    int other_slot = g->players[seat ^ 1];
    g->players[seat] = -1;
    g->ready[seat] = 0;
    g->ships_left[seat] = 0;
    if (other_slot != -1 && g->winner_slot == -1) {
        printf("[GAME_END] opponent left. Winner: [PLAYER slot=%d login=\"%s\"], [GAME id=%d name=\"%s\"] closed.\n",
            other_slot,
            state->players[other_slot].login,
            game_idx,
            g->name);
        g->winner_slot = other_slot;
        state->players[other_slot].game_id = -1;
        reset_game(g);
        g->active = 0;
        cleanup_invites_for_game(game_idx);
    }
    if (g->players[0] == -1 && g->players[1] == -1) {
        reset_game(g);
        g->active = 0;
        cleanup_invites_for_game(game_idx);
    }
    state->players[slot].game_id = -1;
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Вы вышли из игры.");
    printf("[QUIT_GAME] [PLAYER slot=%d login=\"%s\"] left [GAME id=%d].\n",
        slot, state->players[slot].login, game_idx);
}

void handle_fire(int slot, const Request *req, Response *resp) {
    int game_idx = state->players[slot].game_id;
    if (game_idx < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Сначала войдите в игру.");
        return;
    }
    Game *g = &state->games[game_idx];
    if (g->players[1] == -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Нет соперника.");
        return;
    }
    int seat = seat_for_player(g, slot);
    if (seat < 0) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Ошибка игры.");
        return;
    }
    if (g->winner_slot != -1) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Игра завершена.");
        fill_boards(g, seat, resp);
        resp->winner_slot = g->winner_slot;
        return;
    }
    if (g->turn != seat) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Сейчас ход соперника.");
        return;
    }
    int x = req->arg1;
    int y = req->arg2;
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
        resp->code = 1;
        snprintf(resp->message, MAX_MESSAGE, "Координаты вне поля.");
        return;
    }
    int enemy = seat ^ 1;
    CellState *cell = &g->boards[enemy][y][x];
    if (*cell == CELL_SHIP) {
        *cell = CELL_HIT;
        g->ships_left[enemy]--;

        if (g->ships_left[enemy] <= 0) {
            g->winner_slot = slot;

            int game_id = state->players[slot].game_id;

            int p0 = g->players[0];
            int p1 = g->players[1];

            printf("[GAME_END] Winner: [PLAYER slot=%d login=\"%s\"], game_id=%d name=\"%s\".\n",
                slot, state->players[slot].login, game_id, g->name);

            if (p0 != -1) state->players[p0].game_id = -1;
            if (p1 != -1) state->players[p1].game_id = -1;

            cleanup_invites_for_game(game_id);

            reset_game(g);
            g->active = 0;

            resp->code = 0;
            snprintf(resp->message, MAX_MESSAGE, "Вы победили! Все корабли противника уничтожены.");
            resp->winner_slot = slot;
            resp->your_turn = 0;
            fill_boards(g, seat, resp);
            return;
        }

        snprintf(resp->message, MAX_MESSAGE, "Попадание!");
    } else {
        *cell = CELL_MISS;
        g->turn ^= 1;
        snprintf(resp->message, MAX_MESSAGE, "Мимо.");
    }
    resp->code = 0;
    resp->winner_slot = g->winner_slot;
    resp->your_turn = (g->turn == seat && g->winner_slot == -1);
    fill_boards(g, seat, resp);
}

void handle_logout(int slot, Response *resp) {
    char saved_login[MAX_NAME];
    strcpy(saved_login, state->players[slot].login);
    handle_quit_game(slot, resp);
    cleanup_invites_for_player(slot);
    reset_player_slot(slot);
    resp->code = 0;
    snprintf(resp->message, MAX_MESSAGE, "Вы вышли из системы.");
    printf("[LOGOUT] [PLAYER slot=%d login=\"%s\"] disconnected.\n",
        slot, saved_login);
}

void process_request(int slot) {
    Request req = state->requests[slot];
    Response resp;
    memset(&resp, 0, sizeof(resp));

    switch (req.type) {
    case REQ_REGISTER:
        handle_register(slot, &req, &resp);
        break;
    case REQ_LIST_GAMES:
        handle_list_games(&resp);
        break;
    case REQ_CREATE_GAME:
        handle_create_game(slot, &req, &resp);
        break;
    case REQ_JOIN_GAME:
        handle_join_game(slot, &req, &resp);
        break;
    case REQ_SEND_INVITE:
        handle_send_invite(slot, &req, &resp);
        break;
    case REQ_LIST_INVITES:
        handle_list_invites(slot, &resp);
        break;
    case REQ_ACCEPT_INVITE:
        handle_accept_invite(slot, &req, &resp);
        break;
    case REQ_DECLINE_INVITE:
        handle_decline_invite(slot, &req, &resp);
        break;
    case REQ_GAME_STATUS:
        handle_game_status(slot, &resp);
        break;
    case REQ_FIRE:
        handle_fire(slot, &req, &resp);
        break;
    case REQ_QUIT_GAME:
        handle_quit_game(slot, &resp);
        break;
    case REQ_LOGOUT:
        handle_logout(slot, &resp);
        break;
    default:
        resp.code = 1;
        snprintf(resp.message, MAX_MESSAGE, "Неизвестный запрос.");
        break;
    }

    state->responses[slot] = resp;
    state->request_ready[slot] = 0;
    sem_post(resp_sems[slot]);
}

void server_loop() {
    printf("Server started. Waiting for requests...\n");
    while (running) {
        if (sem_wait(request_sem) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("sem_wait request");
            break;
        }
        if (!running) {
            break;
        }
        int handled = 0;
        for (int i = 0; i < MAX_PLAYERS && !handled; i++) {
            sem_wait(slot_sems[i]);
            if (state->request_ready[i]) {
                process_request(i);
                handled = 1;
            }
            sem_post(slot_sems[i]);
        }
    }
    printf("Server shutting down.\n");
}

int main() {
    srand((unsigned int)time(NULL));
    if (setup_ipc() != 0) {
        fprintf(stderr, "Error while initializing IPC.\n");
        return 1;
    }
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    server_loop();
    cleanup_ipc();
    return 0;
}
