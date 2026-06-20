#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"

#define MAX_USERNAME 32
#define MAX_ROLE 10
#define MAX_DATA 2048
#define MAX_USERS 100

// Command Types
#define CMD_LOGIN 1
#define CMD_GET_MARKET_DATA 2
#define CMD_BUY 3
#define CMD_SELL 4
#define CMD_GET_PORTFOLIO 5
#define CMD_HALT_TRADING 6
#define CMD_KICK_USER 7
#define CMD_CANCEL 8
#define CMD_CREATE_ACCOUNT 9
#define CMD_GET_MY_ORDERS 10
#define CMD_SHOW_DETAILS 11

// Roles
#define ROLE_ADMIN "Admin"
#define ROLE_TRADER "User"
#define ROLE_GUEST "Guest"

// Response Types
#define RESP_OK 100
#define RESP_ERROR 101
#define RESP_MARKET_DATA 102
#define RESP_PORTFOLIO 103
#define RESP_DENIED 104
#define RESP_MY_ORDERS 105
#define RESP_SHOW_DETAILS 106

typedef struct {
    int type;
    char username[MAX_USERNAME];
    char role[MAX_ROLE];
    float price;
    int qty;
    int order_id;
    char data[MAX_DATA];
} Message;

#define IPC_MQ_NAME "/stockx_mq"

typedef struct {
    char buyer[MAX_USERNAME];
    char seller[MAX_USERNAME];
    float price;
    int qty;
    time_t timestamp;
} TradeTicket;

typedef struct {
    char username[MAX_USERNAME];
    char role[MAX_ROLE];
    int stock_balance;
    float cash_balance;
    pthread_mutex_t portfolio_mutex;
    int connected;
    int socket_fd;
} UserProfile;

typedef struct Order {
    int id;
    char username[MAX_USERNAME];
    float price;
    int qty;
    struct Order *next;
} Order;

#endif
