#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <mqueue.h>
#include <fcntl.h>
#include <signal.h>
#include "common.h"
#include "engine.h"

int connected_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_running = 1;
mqd_t mq;

void handle_sigint(int sig) {
    printf("\nShutting down server safely...\n");
    double total_cash = 0;
    int total_stock = 0;
    for (int i=0; i<num_users; i++) {
        total_cash += users[i].cash_balance;
        total_stock += users[i].stock_balance;
    }
    double pending_buy_cash = 0;
    int pending_sell_stock = 0;
    Order *curr = bids;
    while(curr) { pending_buy_cash += (curr->price * curr->qty); curr = curr->next; }
    curr = asks;
    while(curr) { pending_sell_stock += curr->qty; curr = curr->next; }
    printf("FINAL_STATE: total_cash=%.2f total_stock=%d pending_buy_cash=%.2f pending_sell_stock=%d\n", total_cash, total_stock, pending_buy_cash, pending_sell_stock);
    server_running = 0;
    mq_close(mq);
    mq_unlink(IPC_MQ_NAME);
    exit(0);
}

void send_response(int sock, int type, const char* data) {
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = type;
    if (data) {
        snprintf(msg.data, MAX_DATA, "%s", data);
    }
    send(sock, &msg, sizeof(Message), 0);
}

void* client_worker(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    Message msg;
    UserProfile *current_user = NULL;
    
    while(1) {
        int bytes = recv(sock, &msg, sizeof(Message), 0);
        if (bytes <= 0) break; 
        
        if (msg.type == CMD_CREATE_ACCOUNT) {
            if (!username_is_valid(msg.username)) {
                send_response(sock, RESP_ERROR, "Username must be 1-31 letters, digits or underscores.");
                continue;
            }
            if (strcmp(msg.role, ROLE_TRADER) != 0 && strcmp(msg.role, ROLE_GUEST) != 0) {
                send_response(sock, RESP_ERROR, "Invalid role requested.");
                continue;
            }
            UserProfile *u = create_user(msg.username, msg.role);
            if (!u) {
                send_response(sock, RESP_ERROR, "That username is already taken.");
            } else {
                send_response(sock, RESP_OK, "Account created. You can now log in.");
            }
            continue;
        }

        if (msg.type == CMD_LOGIN) {
            current_user = find_user(msg.username);
            if (current_user) {
                current_user->connected = 1;
                current_user->socket_fd = sock;
                Message resp;
                memset(&resp, 0, sizeof(Message));
                resp.type = RESP_OK;
                strncpy(resp.role, current_user->role, MAX_ROLE);
                send(sock, &resp, sizeof(Message), 0);
            } else {
                send_response(sock, RESP_ERROR, "No account with that username. Create one first.");
            }
            continue;
        }
        
        if (!current_user) {
            send_response(sock, RESP_ERROR, "Not logged in.");
            continue;
        }
        
        if (msg.type == CMD_BUY || msg.type == CMD_SELL || msg.type == CMD_GET_PORTFOLIO || msg.type == CMD_CANCEL) {
            if (strcmp(current_user->role, ROLE_GUEST) == 0) {
                send_response(sock, RESP_DENIED, "Guest cannot trade.");
                continue;
            }
        }
        
        if (msg.type == CMD_HALT_TRADING || msg.type == CMD_KICK_USER) {
            if (strcmp(current_user->role, ROLE_ADMIN) != 0) {
                send_response(sock, RESP_DENIED, "Admin only.");
                continue;
            }
        }
        
        if (msg.type == CMD_GET_MARKET_DATA) {
            char mdata[MAX_DATA];
            format_market_data(mdata);
            if (strcmp(current_user->role, ROLE_ADMIN) == 0) {
                char admin_data[64];
                pthread_mutex_lock(&clients_mutex);
                sprintf(admin_data, "|Clients: %d", connected_clients - 1);
                pthread_mutex_unlock(&clients_mutex);
                strcat(mdata, admin_data);
            }
            send_response(sock, RESP_MARKET_DATA, mdata);
        }
        else if (msg.type == CMD_BUY) {
            if (trading_halted) send_response(sock, RESP_ERROR, "Trading halted.");
            else process_buy(current_user->username, msg.price, msg.qty, sock);
        }
        else if (msg.type == CMD_SELL) {
            if (trading_halted) send_response(sock, RESP_ERROR, "Trading halted.");
            else process_sell(current_user->username, msg.price, msg.qty, sock);
        }
        else if (msg.type == CMD_CANCEL) {
            process_cancel(current_user->username, msg.order_id, sock);
        }
        else if (msg.type == CMD_GET_PORTFOLIO) {
            char pdata[MAX_DATA];
            pthread_mutex_lock(&current_user->portfolio_mutex);
            snprintf(pdata, MAX_DATA, "Stock: %d, Cash: $%.2f", current_user->stock_balance, current_user->cash_balance);
            pthread_mutex_unlock(&current_user->portfolio_mutex);
            send_response(sock, RESP_PORTFOLIO, pdata);
        }
        else if (msg.type == CMD_GET_MY_ORDERS) {
            if (strcmp(current_user->role, ROLE_GUEST) == 0) {
                send_response(sock, RESP_DENIED, "Guest cannot have orders.");
            } else {
                char my_orders[MAX_DATA];
                format_my_orders(current_user->username, my_orders);
                send_response(sock, RESP_MY_ORDERS, my_orders);
            }
        }
        else if (msg.type == CMD_HALT_TRADING) {
            trading_halted = !trading_halted;
            send_response(sock, RESP_OK, trading_halted ? "Trading Halted." : "Trading Resumed.");
        }
        else if (msg.type == CMD_KICK_USER) {
            UserProfile *u = find_user(msg.data);
            if (u && u->connected && u->socket_fd != -1) {
                close(u->socket_fd);
                u->connected = 0;
                send_response(sock, RESP_OK, "User kicked.");
            } else {
                send_response(sock, RESP_ERROR, "Not found.");
            }
        }
        else if (msg.type == CMD_SHOW_DETAILS) {
            if (strcmp(current_user->role, ROLE_ADMIN) != 0) {
                send_response(sock, RESP_DENIED, "Admin only.");
            } else {
                char details[MAX_DATA] = "";
                pthread_mutex_lock(&users_mutex);
                for (int i = 0; i < num_users; i++) {
                    if (strcmp(users[i].role, ROLE_ADMIN) == 0) {
                        continue; 
                    }

                    char line[256];
                    snprintf(line, sizeof(line), "> %-12s | Role: %-5s | Stock: %-5d | Cash: $%.2f\n", users[i].username, users[i].role, users[i].stock_balance, users[i].cash_balance);
                    strncat(details, line, MAX_DATA - strlen(details) - 1);
                }
                pthread_mutex_unlock(&users_mutex);
                send_response(sock, RESP_SHOW_DETAILS, details);
            }
        }
    }
    
    if (current_user) {
        current_user->connected = 0;
        current_user->socket_fd = -1;
    }
    close(sock);
    pthread_mutex_lock(&clients_mutex);
    connected_clients--;
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    
    UserProfile *admin = &users[num_users++];
    memset(admin, 0, sizeof(UserProfile));
    strcpy(admin->username, "admin");
    strcpy(admin->role, ROLE_ADMIN);
    admin->stock_balance = 0;
    admin->cash_balance = 0.0;
    pthread_mutex_init(&admin->portfolio_mutex, NULL);
    admin->connected = 0;
    admin->socket_fd = -1;

    mq_unlink(IPC_MQ_NAME);
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(TradeTicket);
    attr.mq_curmsgs = 0;
    
    mq = mq_open(IPC_MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(1);
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(1);
    }
    
    listen(server_fd, 10);
    printf("Server listening on port %d\n", SERVER_PORT);
    
    while(server_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        
        if (*client_sock >= 0) {
            pthread_mutex_lock(&clients_mutex);
            connected_clients++;
            pthread_mutex_unlock(&clients_mutex);
            
            pthread_t thread;
            pthread_create(&thread, NULL, client_worker, client_sock);
            pthread_detach(thread);
        } else {
            free(client_sock);
        }
    }
    
    mq_close(mq);
    mq_unlink(IPC_MQ_NAME);
    return 0;
}