#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

static SharedState *state = NULL;
static int shm_fd = -1;
static sem_t *request_sem = NULL;
static sem_t *slot_sems[MAX_PLAYERS];
static sem_t *resp_sems[MAX_PLAYERS];
static int my_slot = -1;
static char my_login[MAX_NAME];

static void build_sem_name(const char *prefix, int idx, char *out, size_t len) {
    snprintf(out, len, "%s%d", prefix, idx);
}

static int attach_ipc(void) {
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        fprintf(stderr, "Не удалось подключиться к серверу. Запустите ./server\n");
        return -1;
    }
    state = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    request_sem = sem_open(SEM_REQUEST_NAME, 0);
    if (request_sem == SEM_FAILED) {
        perror("sem_open request");
        return -1;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        char name[64];
        build_sem_name(SEM_SLOT_PREFIX, i, name, sizeof(name));
        slot_sems[i] = sem_open(name, 0);
        if (slot_sems[i] == SEM_FAILED) {
            perror("sem_open slot");
            return -1;
        }
        build_sem_name(SEM_RESP_PREFIX, i, name, sizeof(name));
        resp_sems[i] = sem_open(name, 0);
        if (resp_sems[i] == SEM_FAILED) {
            perror("sem_open resp");
            return -1;
        }
    }
    return 0;
}

static void close_ipc(void) {
    if (state && state != MAP_FAILED) {
        munmap(state, sizeof(SharedState));
    }
    if (shm_fd >= 0) {
        close(shm_fd);
    }
    if (request_sem && request_sem != SEM_FAILED) {
        sem_close(request_sem);
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (slot_sems[i] && slot_sems[i] != SEM_FAILED) {
            sem_close(slot_sems[i]);
        }
        if (resp_sems[i] && resp_sems[i] != SEM_FAILED) {
            sem_close(resp_sems[i]);
        }
    }
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        sem_wait(slot_sems[i]);
        int available = !state->players[i].active && state->request_ready[i] == 0;
        sem_post(slot_sems[i]);
        if (available) {
            return i;
        }
    }
    return -1;
}

static int send_request(const Request *req, Response *resp) {
    if (my_slot < 0) {
        return -1;
    }
    sem_wait(slot_sems[my_slot]);
    if (state->request_ready[my_slot]) {
        fprintf(stderr, "Предыдущий запрос еще обрабатывается.\n");
        sem_post(slot_sems[my_slot]);
        return -1;
    }
    state->requests[my_slot] = *req;
    state->request_ready[my_slot] = 1;
    sem_post(slot_sems[my_slot]);
    sem_post(request_sem);

    while (sem_wait(resp_sems[my_slot]) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("sem_wait resp");
        return -1;
    }
    sem_wait(slot_sems[my_slot]);
    *resp = state->responses[my_slot];
    sem_post(slot_sems[my_slot]);
    return 0;
}

static void print_board(const char *title, CellState board[BOARD_SIZE][BOARD_SIZE]) {
    printf("%s\n   ", title);
    for (int x = 0; x < BOARD_SIZE; x++) {
        printf(" %d", x + 1);
    }
    printf("\n");
    for (int y = 0; y < BOARD_SIZE; y++) {
        printf("%2d ", y + 1);
        for (int x = 0; x < BOARD_SIZE; x++) {
            char c = '.';
            switch (board[y][x]) {
            case CELL_EMPTY:
                c = '.';
                break;
            case CELL_SHIP:
                c = 'O';
                break;
            case CELL_MISS:
                c = '*';
                break;
            case CELL_HIT:
                c = 'X';
                break;
            }
            printf(" %c", c);
        }
        printf("\n");
    }
}

static void print_boards_if_available(const Response *resp) {
    if (!resp->has_boards) {
        return;
    }
    print_board("Ваше поле:", (CellState (*)[BOARD_SIZE])resp->your_board);
    print_board("Поле соперника (видны только выстрелы):", (CellState (*)[BOARD_SIZE])resp->enemy_board);
    if (resp->winner_slot != -1) {
        if (resp->winner_slot == my_slot) {
            printf("Вы победили!\n");
        } else {
            printf("Победил соперник.\n");
        }
    } else {
        printf("Ход: %s\n", resp->your_turn ? "ваш" : "соперника");
    }
}

static void prompt_line(const char *label, char *out, size_t len) {
    printf("%s", label);
    fflush(stdout);
    if (fgets(out, (int)len, stdin)) {
        size_t n = strlen(out);
        if (n > 0 && out[n - 1] == '\n') {
            out[n - 1] = '\0';
        }
    } else {
        out[0] = '\0';
    }
}

static void login_flow(void) {
    Response resp;
    while (1) {
        my_slot = find_free_slot();
        if (my_slot < 0) {
            printf("Нет свободных слотов. Попробуйте позже.\n");
            exit(1);
        }
        printf("Использую слот %d.\n", my_slot);
        prompt_line("Введите логин: ", my_login, sizeof(my_login));
        Request req;
        memset(&req, 0, sizeof(req));
        req.type = REQ_REGISTER;
        req.from_slot = my_slot;
        strncpy(req.text, my_login, MAX_NAME - 1);
        if (send_request(&req, &resp) == 0 && resp.code == 0) {
            printf("%s\n", resp.message);
            break;
        }
        printf("Ошибка: %s\n", resp.message);
        sleep(1);
    }
}

static int current_game_id(void) {
    int gid = -1;
    sem_wait(slot_sems[my_slot]);
    gid = state->players[my_slot].game_id;
    sem_post(slot_sems[my_slot]);
    return gid;
}

static void show_menu(void) {
    int gid = current_game_id();
    printf("\n==== Морской бой ====\n");
    printf("Вы: %s | Игра: %s\n", my_login, gid >= 0 ? "в игре" : "нет");
    printf("1. Список игр\n");
    printf("2. Создать игру\n");
    printf("3. Присоединиться к игре по имени\n");
    printf("4. Отправить приглашение (нужна своя игра)\n");
    printf("5. Показать приглашения\n");
    printf("6. Принять приглашение (логин пригласившего)\n");
    printf("7. Отклонить приглашения\n");
    printf("8. Статус игры / показать поле\n");
    printf("9. Сделать выстрел\n");
    printf("10. Покинуть игру\n");
    printf("0. Выход\n> ");
    fflush(stdout);
}

static void handle_response(const Response *resp) {
    if (resp->message[0]) {
        printf("%s\n", resp->message);
    }
    print_boards_if_available(resp);
}

int main(void) {
    if (attach_ipc() != 0) {
        return 1;
    }
    login_flow();
    int running = 1;
    while (running) {
        show_menu();
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        int choice = atoi(line);
        Response resp;
        Request req;
        memset(&req, 0, sizeof(req));
        req.from_slot = my_slot;
        switch (choice) {
        case 1:
            req.type = REQ_LIST_GAMES;
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 2:
            req.type = REQ_CREATE_GAME;
            prompt_line("Имя игры: ", req.text, sizeof(req.text));
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 3:
            req.type = REQ_JOIN_GAME;
            prompt_line("Имя игры: ", req.text, sizeof(req.text));
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 4:
            req.type = REQ_SEND_INVITE;
            prompt_line("Логин игрока: ", req.text, sizeof(req.text));
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 5:
            req.type = REQ_LIST_INVITES;
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 6:
            req.type = REQ_ACCEPT_INVITE;
            prompt_line("Логин пригласившего (или Enter для первого): ", req.text, sizeof(req.text));
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 7:
            req.type = REQ_DECLINE_INVITE;
            prompt_line("Логин пригласившего (Enter чтобы отклонить все): ", req.text, sizeof(req.text));
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 8:
            req.type = REQ_GAME_STATUS;
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 9: {
            req.type = REQ_FIRE;
            int x = 0, y = 0;
            printf("Введите координаты выстрела (x y от 1 до %d): ", BOARD_SIZE);
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin)) {
                sscanf(line, "%d %d", &x, &y);
                req.arg1 = x - 1;
                req.arg2 = y - 1;
                send_request(&req, &resp);
                handle_response(&resp);
            }
            break;
        }
        case 10:
            req.type = REQ_QUIT_GAME;
            send_request(&req, &resp);
            handle_response(&resp);
            break;
        case 0:
            req.type = REQ_LOGOUT;
            send_request(&req, &resp);
            handle_response(&resp);
            running = 0;
            break;
        default:
            printf("Неизвестная команда.\n");
            break;
        }
    }
    close_ipc();
    return 0;
}
