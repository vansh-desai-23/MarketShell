#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <ncurses.h>
#include "common.h"

int sock;
char current_username[MAX_USERNAME];
char current_role[MAX_ROLE];
char market_data_bids[MAX_DATA] = "BIDS:\n";
char market_data_asks[MAX_DATA] = "ASKS:\n";
char admin_info[MAX_DATA] = "";
char portfolio_data[MAX_DATA] = "Stock: 0, Cash: $0.00";
char my_orders_data[MAX_DATA] = "None";
char last_message[MAX_DATA] = "";
char admin_details_data[MAX_DATA] = " Execute 'showdetails' to pull user directory.";

WINDOW *display_win;
WINDOW *input_win;

pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sock_mutex = PTHREAD_MUTEX_INITIALIZER;

void render_dashboard() {
    pthread_mutex_lock(&ui_mutex);
    werase(display_win);
    
    mvwprintw(display_win, 0, 0, "================================================================================");
    if (strlen(admin_info) > 0) {
        mvwprintw(display_win, 1, 0, " STOCKX ENGINE | Status: Connected | User: %s | Role: %s | %s", current_username, current_role, admin_info);
    } else {
        mvwprintw(display_win, 1, 0, " STOCKX ENGINE | Status: Connected | User: %s | Role: %s", current_username, current_role);
    }
    mvwprintw(display_win, 2, 0, " System Msg: %s", last_message);
    mvwprintw(display_win, 3, 0, "================================================================================");
    
    mvwprintw(display_win, 4, 0, " LIVE ORDER BOOK");
    mvwprintw(display_win, 5, 0, " %-34s | %-34s", "BUY ORDERS (Qty @ Price)", "SELL ORDERS (Qty @ Price)");
    mvwprintw(display_win, 6, 0, " -----------------------------------|-----------------------------------");
    
    char bids_copy[MAX_DATA], asks_copy[MAX_DATA];
    strncpy(bids_copy, market_data_bids, MAX_DATA);
    strncpy(asks_copy, market_data_asks, MAX_DATA);
    
    char *saveptr_bids, *saveptr_asks;
    char *bid_line = strtok_r(bids_copy, "\n", &saveptr_bids);
    char *ask_line = strtok_r(asks_copy, "\n", &saveptr_asks);
    
    if(bid_line) bid_line = strtok_r(NULL, "\n", &saveptr_bids); 
    if(ask_line) ask_line = strtok_r(NULL, "\n", &saveptr_asks); 
    
    for(int i = 0; i < 5; i++) {
        mvwprintw(display_win, 7 + i, 0, " %-34s | %-34s", 
               bid_line ? bid_line : "", 
               ask_line ? ask_line : "");
               
        if(bid_line) bid_line = strtok_r(NULL, "\n", &saveptr_bids);
        if(ask_line) ask_line = strtok_r(NULL, "\n", &saveptr_asks);
    }
    
    mvwprintw(display_win, 12, 0, "================================================================================");
    
    if (strcmp(current_role, "User") == 0) {
        int stock = 0;
        float cash = 0.0;
        sscanf(portfolio_data, "Stock: %d, Cash: $%f", &stock, &cash);
        
        mvwprintw(display_win, 13, 0, " YOUR PORTFOLIO");
        mvwprintw(display_win, 14, 0, " Stock Balance: %d", stock);
        mvwprintw(display_win, 15, 0, " Available Cash: $%.2f", cash);
        
        mvwprintw(display_win, 16, 0, "--------------------------------------------------------------------------------");
        mvwprintw(display_win, 17, 0, " YOUR ACTIVE ORDERS");
        
        char my_orders_copy[MAX_DATA];
        strncpy(my_orders_copy, my_orders_data, MAX_DATA);
        char *saveptr_orders;
        char *order_token = strtok_r(my_orders_copy, "\n", &saveptr_orders);
        
        int row_index = 18;
        int count = 0;
        while(order_token && count < 5) {
            mvwprintw(display_win, row_index++, 0, " > %s", order_token);
            order_token = strtok_r(NULL, "\n", &saveptr_orders);
            count++;
        }
    } else if (strcmp(current_role, "Admin") == 0) {
        mvwprintw(display_win, 13, 0, " SYSTEM USERS DIRECTORY (Earnings & Balances)");
        mvwprintw(display_win, 14, 0, "--------------------------------------------------------------------------------");
        
        char admin_details_copy[MAX_DATA];
        strncpy(admin_details_copy, admin_details_data, MAX_DATA);
        char *saveptr_details;
        char *detail_token = strtok_r(admin_details_copy, "\n", &saveptr_details);
        
        int row_index = 15;
        int count = 0;
        while(detail_token && count < 8) {
            mvwprintw(display_win, row_index++, 0, " %s", detail_token);
            detail_token = strtok_r(NULL, "\n", &saveptr_details);
            count++;
        }
    }
    
    int max_y, max_x;
    getmaxyx(display_win, max_y, max_x);
    (void)max_x;

    mvwprintw(display_win, max_y - 2, 0, "================================================================================");
    if (strcmp(current_role, "Admin") == 0) {
        mvwprintw(display_win, max_y - 1, 0, " Commands: showdetails, halt, kick <user>, exit");
    } else if (strcmp(current_role, "User") == 0) {
        mvwprintw(display_win, max_y - 1, 0, " Commands: buy <qty> <price>, sell <qty> <price>, cancel <id>, exit");
    } else {
        mvwprintw(display_win, max_y - 1, 0, " Commands: exit (Guest can only view market data)");
    }
    
    wrefresh(display_win);
    wrefresh(input_win); 
    pthread_mutex_unlock(&ui_mutex);
}

void* receive_thread(void* arg) {
    Message msg;
    while(1) {
        int bytes = recv(sock, &msg, sizeof(Message), 0);
        if (bytes <= 0) {
            endwin();
            printf("\nServer disconnected.\n");
            exit(0);
        }
        
        pthread_mutex_lock(&ui_mutex);
        if (msg.type == RESP_MARKET_DATA) {
            char *delim1 = strchr(msg.data, '|');
            if (delim1) {
                *delim1 = '\0';
                strncpy(market_data_bids, msg.data, MAX_DATA - 1);
                market_data_bids[MAX_DATA - 1] = '\0';
                
                char *delim2 = strchr(delim1 + 1, '|');
                if (delim2) {
                    *delim2 = '\0';
                    strncpy(market_data_asks, delim1 + 1, MAX_DATA - 1);
                    market_data_asks[MAX_DATA - 1] = '\0';
                    strncpy(admin_info, delim2 + 1, MAX_DATA - 1);
                    admin_info[MAX_DATA - 1] = '\0';
                } else {
                    strncpy(market_data_asks, delim1 + 1, MAX_DATA - 1);
                    market_data_asks[MAX_DATA - 1] = '\0';
                    admin_info[0] = '\0';
                }
            }
        }
        else if (msg.type == RESP_PORTFOLIO) {
            strncpy(portfolio_data, msg.data, MAX_DATA - 1);
            portfolio_data[MAX_DATA - 1] = '\0';
        }
        else if (msg.type == RESP_MY_ORDERS) {
            strncpy(my_orders_data, msg.data, MAX_DATA - 1);
            my_orders_data[MAX_DATA - 1] = '\0';
        }
        else if (msg.type == RESP_SHOW_DETAILS) {
            strncpy(admin_details_data, msg.data, MAX_DATA - 1);
            admin_details_data[MAX_DATA - 1] = '\0';
            strncpy(last_message, "User directory refreshed successfully.", MAX_DATA - 1); 
        }
        else if (msg.type == RESP_OK || msg.type == RESP_ERROR || msg.type == RESP_DENIED) {
            strncpy(last_message, msg.data, MAX_DATA - 1);
            last_message[MAX_DATA - 1] = '\0';
        }
        pthread_mutex_unlock(&ui_mutex);
        
        render_dashboard();
    }
    return NULL;
}

void* polling_thread(void* arg) {
    while(1) {
        Message req;
        memset(&req, 0, sizeof(Message));
        req.type = CMD_GET_MARKET_DATA;
        pthread_mutex_lock(&sock_mutex);
        send(sock, &req, sizeof(Message), 0);
        pthread_mutex_unlock(&sock_mutex);
        
        if (strcmp(current_role, "User") == 0) {
            req.type = CMD_GET_PORTFOLIO;
            pthread_mutex_lock(&sock_mutex);
            send(sock, &req, sizeof(Message), 0);
            pthread_mutex_unlock(&sock_mutex);
            req.type = CMD_GET_MY_ORDERS;
            pthread_mutex_lock(&sock_mutex);
            send(sock, &req, sizeof(Message), 0);
            pthread_mutex_unlock(&sock_mutex);
        }
        
        sleep(2);
    }
    return NULL;
}

int main() {
    int choice = 0;
    while(1) {
        printf("\n1) Login\n2) Create Account\nChoice: ");
        if (scanf("%d", &choice) != 1) return 1;
        
        if (choice == 1 || choice == 2) {
            break;
        }
        printf("Invalid choice.\n");
    }

    if (choice == 2) {
        printf("Enter new username: ");
        if (scanf("%31s", current_username) != 1) return 1;
        printf("Enter role (User/Guest): ");
        char req_role[MAX_ROLE];
        if (scanf("%9s", req_role) != 1) return 1;
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Connection failed");
            return 1;
        }

        Message create_msg;
        memset(&create_msg, 0, sizeof(Message));
        create_msg.type = CMD_CREATE_ACCOUNT;
        strncpy(create_msg.username, current_username, MAX_USERNAME);
        strncpy(create_msg.role, req_role, MAX_ROLE);
        send(sock, &create_msg, sizeof(Message), 0);
        
        Message resp;
        recv(sock, &resp, sizeof(Message), 0);
        if (resp.type != RESP_OK) {
            printf("Create account failed: %s\n", resp.data);
            return 1;
        }
        printf("Account created successfully. Proceeding to login...\n");
    } else {
        printf("Enter username: ");
        if (scanf("%31s", current_username) != 1) return 1;
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Connection failed");
            return 1;
        }
    }

    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    Message login_msg;
    memset(&login_msg, 0, sizeof(Message));
    login_msg.type = CMD_LOGIN;
    strncpy(login_msg.username, current_username, MAX_USERNAME);
    send(sock, &login_msg, sizeof(Message), 0);
    
    Message resp;
    recv(sock, &resp, sizeof(Message), 0);
    if (resp.type != RESP_OK) {
        printf("Login failed: %s\n", resp.data);
        close(sock);
        return 1;
    }
    
    strncpy(current_role, resp.role, MAX_ROLE);
    
    initscr();
    cbreak();
    echo();
    
    int term_h, term_w;
    getmaxyx(stdscr, term_h, term_w);
    int disp_h = (term_h * 8) / 10;
    int inp_h = term_h - disp_h;
    
    display_win = newwin(disp_h, term_w, 0, 0);
    input_win = newwin(inp_h, term_w, disp_h, 0);
    
    render_dashboard();
    
    pthread_t recv_t, poll_t;
    pthread_create(&recv_t, NULL, receive_thread, NULL);
    pthread_create(&poll_t, NULL, polling_thread, NULL);
    
    char input[256];
    while(1) {
        werase(input_win);
        mvwprintw(input_win, 0, 0, "> ");
        wrefresh(input_win);
        
        if (mvwgetnstr(input_win, 0, 2, input, sizeof(input) - 1) != ERR) {
            if (strlen(input) == 0) continue;
            
            char cmd[32];
            float price;
            int qty;
            int order_id;
            char target[32];
            
            Message req;
            memset(&req, 0, sizeof(Message));
            strncpy(req.username, current_username, MAX_USERNAME);
            
            if (sscanf(input, "%s %d %f", cmd, &qty, &price) == 3) {
                if (strcmp(cmd, "buy") == 0) {
                    req.type = CMD_BUY;
                    req.qty = qty;
                    req.price = price;
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                } else if (strcmp(cmd, "sell") == 0) {
                    req.type = CMD_SELL;
                    req.qty = qty;
                    req.price = price;
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                }
            } else if (sscanf(input, "%s %d", cmd, &order_id) == 2) {
                if (strcmp(cmd, "cancel") == 0) {
                    req.type = CMD_CANCEL;
                    req.order_id = order_id;
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                }
            } else if (sscanf(input, "%s %s", cmd, target) == 2) {
                if (strcmp(cmd, "kick") == 0) {
                    req.type = CMD_KICK_USER;
                    strncpy(req.data, target, MAX_DATA);
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                }
            } else if (sscanf(input, "%s", cmd) == 1) {
                if (strcmp(cmd, "showdetails") == 0) {
                    req.type = CMD_SHOW_DETAILS;
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                } else if (strcmp(cmd, "halt") == 0) {
                    req.type = CMD_HALT_TRADING;
                    pthread_mutex_lock(&sock_mutex);
                    send(sock, &req, sizeof(Message), 0);
                    pthread_mutex_unlock(&sock_mutex);
                } else if (strcmp(cmd, "exit") == 0) {
                    break;
                }
            }
        }
    }
    
    endwin();
    close(sock);
    return 0;
}
