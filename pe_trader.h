#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define MAX_ORDER_ID 999999
#define MAX_PRODUCT_NAME_LEN 16
#define MAX_QTY 999999
#define MAX_PRICE 999999
#define BUFFER_SIZE 256

typedef struct {
    char type[5];
    char product_name[MAX_PRODUCT_NAME_LEN+1];
    int quantity;
    int price;
} pe_order;

int open_pipe();
int read_pipe();
int write_pipe();
int process_order();
int is_valid_order();
int order_to_msg();
int order_accepted();
void signal_handler();
int next_order_id();

#endif