#ifndef BATTLESHIP_COMMON_H
#define BATTLESHIP_COMMON_H

#include <semaphore.h>
#include <stdint.h>

#define SHM_NAME "/battleship_shm"
#define SEM_REQUEST_NAME "/bship_req"
#define SEM_SLOT_PREFIX "/bship_slot_"
#define SEM_RESP_PREFIX "/bship_resp_"

#define MAX_PLAYERS 10
#define MAX_GAMES 20
#define MAX_INVITES 20
#define MAX_NAME 32
#define BOARD_SIZE 8
#define MAX_MESSAGE 256

typedef enum {
    REQ_NONE = 0,
    REQ_REGISTER,
    REQ_CREATE_GAME,
    REQ_JOIN_GAME,
    REQ_LIST_GAMES,
    REQ_SEND_INVITE,
    REQ_LIST_INVITES,
    REQ_ACCEPT_INVITE,
    REQ_DECLINE_INVITE,
    REQ_GAME_STATUS,
    REQ_FIRE,
    REQ_QUIT_GAME,
    REQ_LOGOUT
} RequestType;

typedef enum {
    CELL_EMPTY = 0,
    CELL_SHIP,
    CELL_MISS,
    CELL_HIT
} CellState;

typedef struct {
    RequestType type;
    int from_slot;
    int arg1;
    int arg2;
    char text[MAX_NAME];
} Request;

typedef struct {
    int code;
    char message[MAX_MESSAGE];
    int game_id;
    int your_turn;
    int winner_slot;
    int has_boards;
    CellState your_board[BOARD_SIZE][BOARD_SIZE];
    CellState enemy_board[BOARD_SIZE][BOARD_SIZE];
} Response;

typedef struct {
    int active;
    char login[MAX_NAME];
    int game_id;
} Player;

typedef struct {
    int active;
    int from_player;
    int to_player;
    int game_id;
} Invite;

typedef struct {
    int active;
    char name[MAX_NAME];
    int players[2];
    int turn;
    int ready[2];
    int ships_left[2];
    int winner_slot;
    CellState boards[2][BOARD_SIZE][BOARD_SIZE];
} Game;

typedef struct {
    Player players[MAX_PLAYERS];
    Game games[MAX_GAMES];
    Invite invites[MAX_INVITES];
    Request requests[MAX_PLAYERS];
    Response responses[MAX_PLAYERS];
    int request_ready[MAX_PLAYERS];
} SharedState;

#endif
