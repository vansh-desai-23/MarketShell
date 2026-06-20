#ifndef ENGINE_H
#define ENGINE_H

#include <pthread.h>
#include <mqueue.h>
#include "common.h"

extern UserProfile users[MAX_USERS];
extern int num_users;
extern Order *bids;
extern Order *asks;
extern pthread_mutex_t order_book_mutex;
extern int next_order_id;
extern int trading_halted;
extern pthread_mutex_t users_mutex;
extern mqd_t mq;

UserProfile* find_user(const char* username);
UserProfile* create_user(const char* username, const char* role);
int username_is_valid(const char* username);
void format_my_orders(const char* username, char* buffer);
void format_market_data(char *buffer);
void handle_trade_match(Order* bid, Order* ask, int match_qty, float match_price);
void insert_bid(const char* username, float price, int qty);
void insert_ask(const char* username, float price, int qty);
void process_buy(const char* username, float price, int qty, int sock);
void process_sell(const char* username, float price, int qty, int sock);
void process_cancel(const char* username, int order_id, int sock);

void send_response(int sock, int type, const char* data);

#endif
