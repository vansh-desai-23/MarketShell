#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "engine.h"

UserProfile users[MAX_USERS];
int num_users = 0;
Order *bids = NULL;
Order *asks = NULL;
pthread_mutex_t order_book_mutex = PTHREAD_MUTEX_INITIALIZER;
int next_order_id = 1;

int trading_halted = 0;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

UserProfile* find_user(const char* username) {
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return &users[i];
        }
    }
    pthread_mutex_unlock(&users_mutex);
    return NULL;
}

UserProfile* create_user(const char* username, const char* role) {
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return NULL;
        }
    }
    if (num_users >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        return NULL;
    }
    UserProfile *u = &users[num_users++];
    memset(u, 0, sizeof(UserProfile));
    strncpy(u->username, username, MAX_USERNAME - 1);
    u->username[MAX_USERNAME - 1] = '\0';
    strncpy(u->role, role, MAX_ROLE - 1);
    u->role[MAX_ROLE - 1] = '\0';
    u->stock_balance = 100;
    u->cash_balance = 10000.0;
    pthread_mutex_init(&u->portfolio_mutex, NULL);
    u->connected = 0;
    u->socket_fd = -1;
    pthread_mutex_unlock(&users_mutex);
    return u;
}

int username_is_valid(const char* username) {
    size_t len = strlen(username);
    if (len == 0 || len >= MAX_USERNAME) return 0;
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)username[i]) && username[i] != '_') return 0;
    }
    return 1;
}

void format_my_orders(const char* username, char* buffer) {
    pthread_mutex_lock(&order_book_mutex);
    buffer[0] = '\0';
    
    Order *curr = bids;
    while(curr) {
        if (strcmp(curr->username, username) == 0) {
            char line[128];
            snprintf(line, sizeof(line), "BUY %d @ $%.2f (ID:%d)\n", curr->qty, curr->price, curr->id);
            strncat(buffer, line, MAX_DATA - strlen(buffer) - 1);
        }
        curr = curr->next;
    }
    
    curr = asks;
    while(curr) {
        if (strcmp(curr->username, username) == 0) {
            char line[128];
            snprintf(line, sizeof(line), "SELL %d @ $%.2f (ID:%d)\n", curr->qty, curr->price, curr->id);
            strncat(buffer, line, MAX_DATA - strlen(buffer) - 1);
        }
        curr = curr->next;
    }
    
    if (strlen(buffer) == 0) {
        strncpy(buffer, "None", MAX_DATA - 1);
        buffer[MAX_DATA - 1] = '\0';
    }
    pthread_mutex_unlock(&order_book_mutex);
}

void format_market_data(char *buffer) {
    pthread_mutex_lock(&order_book_mutex);
    char bids_str[1024] = "BIDS:\n";
    Order *curr = bids;
    int count = 0;
    while(curr && count < 5) {
        char line[64];
        sprintf(line, "%d @ %.2f (ID:%d)\n", curr->qty, curr->price, curr->id);
        strcat(bids_str, line);
        curr = curr->next;
        count++;
    }
    
    char asks_str[1024] = "ASKS:\n";
    curr = asks;
    count = 0;
    while(curr && count < 5) {
        char line[64];
        sprintf(line, "%d @ %.2f (ID:%d)\n", curr->qty, curr->price, curr->id);
        strcat(asks_str, line);
        curr = curr->next;
        count++;
    }
    pthread_mutex_unlock(&order_book_mutex);
    snprintf(buffer, MAX_DATA, "%s|%s", bids_str, asks_str);
}

void handle_trade_match(Order* bid, Order* ask, int match_qty, float match_price) {
    TradeTicket ticket;
    strncpy(ticket.buyer, bid->username, MAX_USERNAME);
    strncpy(ticket.seller, ask->username, MAX_USERNAME);
    ticket.price = match_price;
    ticket.qty = match_qty;
    ticket.timestamp = time(NULL);
    
    UserProfile *buyer = find_user(bid->username);
    UserProfile *seller = find_user(ask->username);
    
    UserProfile *first_lock = (buyer < seller) ? buyer : seller;
    UserProfile *second_lock = (buyer < seller) ? seller : buyer;
    if (first_lock) pthread_mutex_lock(&first_lock->portfolio_mutex);
    if (second_lock && second_lock != first_lock) pthread_mutex_lock(&second_lock->portfolio_mutex);
    
    if (buyer) {
        buyer->stock_balance += match_qty;
        if (bid->price > match_price) {
            buyer->cash_balance += (bid->price - match_price) * match_qty;
        }
    }
    
    if (seller) {
        seller->cash_balance += (match_price * match_qty);
    }
    
    if (second_lock && second_lock != first_lock) pthread_mutex_unlock(&second_lock->portfolio_mutex);
    if (first_lock) pthread_mutex_unlock(&first_lock->portfolio_mutex);
    
    mq_send(mq, (const char*)&ticket, sizeof(TradeTicket), 0);
}

void insert_bid(const char* username, float price, int qty) {
    Order *new_order = malloc(sizeof(Order));
    new_order->id = next_order_id++;
    strncpy(new_order->username, username, MAX_USERNAME - 1);
    new_order->username[MAX_USERNAME - 1] = '\0';
    new_order->price = price;
    new_order->qty = qty;
    new_order->next = NULL;
    
    if (!bids || new_order->price > bids->price) {
        new_order->next = bids;
        bids = new_order;
    } else {
        Order *curr = bids;
        while (curr->next && curr->next->price >= new_order->price) {
            curr = curr->next;
        }
        new_order->next = curr->next;
        curr->next = new_order;
    }
}

void insert_ask(const char* username, float price, int qty) {
    Order *new_order = malloc(sizeof(Order));
    new_order->id = next_order_id++;
    strncpy(new_order->username, username, MAX_USERNAME - 1);
    new_order->username[MAX_USERNAME - 1] = '\0';
    new_order->price = price;
    new_order->qty = qty;
    new_order->next = NULL;
    
    if (!asks || new_order->price < asks->price) {
        new_order->next = asks;
        asks = new_order;
    } else {
        Order *curr = asks;
        while (curr->next && curr->next->price <= new_order->price) {
            curr = curr->next;
        }
        new_order->next = curr->next;
        curr->next = new_order;
    }
}

void process_buy(const char* username, float price, int qty, int sock) {
    UserProfile *user = find_user(username);
    if (!user) return;
    
    pthread_mutex_lock(&user->portfolio_mutex);
    float cost = price * qty;
    if (user->cash_balance < cost) {
        pthread_mutex_unlock(&user->portfolio_mutex);
        send_response(sock, RESP_ERROR, "Insufficient cash.");
        return;
    }
    user->cash_balance -= cost; 
    pthread_mutex_unlock(&user->portfolio_mutex);

    pthread_mutex_lock(&order_book_mutex);
    int remaining_qty = qty;
    
    while (asks && remaining_qty > 0 && asks->price == price) {
        Order *ask = asks;
        int match_qty = (remaining_qty < ask->qty) ? remaining_qty : ask->qty;
        float match_price = ask->price;
        
        Order dummy_bid;
        strncpy(dummy_bid.username, username, MAX_USERNAME - 1);
        dummy_bid.username[MAX_USERNAME - 1] = '\0';
        dummy_bid.price = price; 
        
        handle_trade_match(&dummy_bid, ask, match_qty, match_price);
        
        remaining_qty -= match_qty;
        ask->qty -= match_qty;
        if (ask->qty == 0) {
            asks = ask->next;
            free(ask);
        }
    }
    
    if (remaining_qty > 0) insert_bid(username, price, remaining_qty);
    pthread_mutex_unlock(&order_book_mutex);
    send_response(sock, RESP_OK, "Buy order processed.");
}

void process_sell(const char* username, float price, int qty, int sock) {
    UserProfile *user = find_user(username);
    if (!user) return;
    
    pthread_mutex_lock(&user->portfolio_mutex);
    if (user->stock_balance < qty) {
        pthread_mutex_unlock(&user->portfolio_mutex);
        send_response(sock, RESP_ERROR, "Insufficient stock.");
        return;
    }
    user->stock_balance -= qty; 
    pthread_mutex_unlock(&user->portfolio_mutex);

    pthread_mutex_lock(&order_book_mutex);
    int remaining_qty = qty;
    
    while (bids && remaining_qty > 0 && bids->price == price) {
        Order *bid = bids;
        int match_qty = (remaining_qty < bid->qty) ? remaining_qty : bid->qty;
        float match_price = bid->price;
        
        Order dummy_ask;
        strncpy(dummy_ask.username, username, MAX_USERNAME - 1);
        dummy_ask.username[MAX_USERNAME - 1] = '\0';
        dummy_ask.price = price;
        
        handle_trade_match(bid, &dummy_ask, match_qty, match_price);
        
        remaining_qty -= match_qty;
        bid->qty -= match_qty;
        if (bid->qty == 0) {
            bids = bid->next;
            free(bid);
        }
    }
    
    if (remaining_qty > 0) insert_ask(username, price, remaining_qty);
    pthread_mutex_unlock(&order_book_mutex);
    send_response(sock, RESP_OK, "Sell order processed.");
}

void process_cancel(const char* username, int order_id, int sock) {
    pthread_mutex_lock(&order_book_mutex);
    Order *prev = NULL;
    Order *curr = bids;
    while(curr) {
        if (curr->id == order_id && strcmp(curr->username, username) == 0) {
            if (prev) prev->next = curr->next;
            else bids = curr->next;
            
            UserProfile *u = find_user(username);
            if (u) {
                pthread_mutex_lock(&u->portfolio_mutex);
                u->cash_balance += (curr->qty * curr->price);
                pthread_mutex_unlock(&u->portfolio_mutex);
            }
            free(curr);
            pthread_mutex_unlock(&order_book_mutex);
            send_response(sock, RESP_OK, "Order cancelled.");
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    
    prev = NULL;
    curr = asks;
    while(curr) {
        if (curr->id == order_id && strcmp(curr->username, username) == 0) {
            if (prev) prev->next = curr->next;
            else asks = curr->next;
            
            UserProfile *u = find_user(username);
            if (u) {
                pthread_mutex_lock(&u->portfolio_mutex);
                u->stock_balance += curr->qty;
                pthread_mutex_unlock(&u->portfolio_mutex);
            }
            free(curr);
            pthread_mutex_unlock(&order_book_mutex);
            send_response(sock, RESP_OK, "Order cancelled.");
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&order_book_mutex);
    send_response(sock, RESP_ERROR, "Order not found.");
}
