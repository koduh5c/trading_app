#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"

#define LOG_PREFIX "[PEX]"
#define MAX_ORDER_ID 999999
#define MAX_QTY 999999
#define MAX_PRICE 999999
#define BUFFER_SIZE 256

typedef struct pe_product_t {
    char p_name[32];
    struct pe_level_t *buy_level;
    int buy_levels;
    struct pe_level_t *sell_level;
    int sell_levels;
} pe_product;

typedef struct pe_position_t {
    char p_name[32];
    long traded_quantity;
    long traded_price;
} pe_position;

typedef struct pe_trader_t {
    int id;
    char bin_fn[32];
    char write_fn[32];
    char read_fn[32];
    int write_fd;
    int read_fd;
    int pid;
    int disconnected;
    int order_count;
    pe_position *positions;
} pe_trader;

typedef struct pe_order_t {
    char command[32];
    int id;
    char p_name[32];
    long quantity;
    long price;
    pe_trader *trader;
    struct pe_order_t *next;
} pe_order;

typedef struct pe_level_t {
    long price;
    long quantity;
    int size;
    pe_order *orders;
    struct pe_level_t *next;
} pe_level;

typedef struct tracker_t {
    void *data;
    struct tracker_t *next;
} tracker;

int parse_products(char *product_fn);
int parse_traders(int argc, char **argv);

int create_fifo(pe_trader *trader, char *fifo_format);
int connect_fifo(pe_trader *trader);
int read_pipe(int *fd, char *buffer);
int write_pipe(int *fd, char *buffer);

void add_tracker(void *data);
void remove_tracker(void *data);
void free_trackers();

pe_level *create_level(pe_order *new_order);
int add_order(pe_product *product, pe_order *new_order);
int remove_order(pe_order *order, int is_amend);
void match_orders(pe_product *product, pe_order *new_order);

void respond_order_filled(pe_order *order_a, pe_order *order_b, int filled_quantity);
void respond_to_trader(char *command, pe_order *order);
void notify_traders(pe_order *order);

void report();

int process_order(char *buffer, pe_order *order);

void sigusr1_handler(int signum, siginfo_t *info, void *context);
void sigchld_handler(int signum, siginfo_t *info, void *context);


#endif