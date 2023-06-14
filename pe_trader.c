#include "pe_trader.h"

volatile int order_id_counter = 0;

int open_pipe(int trader_id, char *pipe_header, int flag) {
    char *pipe_name = (char *)malloc((strlen(pipe_header) + 1) * sizeof(char));
    sprintf(pipe_name, pipe_header, trader_id);
    int fd = open(pipe_name, flag);
    free(pipe_name);
    return fd;
}

int read_pipe(int *fd, char *msg) {
    memset(msg, '\0', BUFFER_SIZE);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (read(*fd, msg + i, 1) == -1) {
            return -1;
        };
        if (msg[i] == ';') {
            break;
        }
    }
    msg[strlen(msg)] = '\0';
    return 1;
}

int write_pipe(int *fd, char *msg) {
    int n = write(*fd, msg, strlen(msg));
    if (n == -1) {
        printf("Trader error: failed to write order to pipe\n");
        return -1;
    } else if (n == 0) {
        printf("Trader error: nothing was written to the pipe\n");
        return -1;
    }
    kill(getppid(), SIGUSR1);
    return 1;
}

int is_valid_order(char *msg) {
    if (strncmp(msg, "MARKET SELL", strlen("MARKET SELL")) == 0) {
        return 1;
    }
    // if (strncmp(msg, "MARKET BUY", strlen("MARKET BUY")) == 0) {
    //     return 1;
    // }
    return -1;
}

int process_order(char *msg, pe_order *order) {
    if (sscanf(msg, "MARKET %s %s %d %d",
               order->type,
               order->product_name,
               &order->quantity,
               &order->price) != 4) {
        return -1;
    }
    // order parameters checked here
    if ((strlen(order->product_name) == 0) || (strlen(order->product_name) > 16)) {
        // printf("Trader error: order product invalid\n");
        return -1;
    }
    if ((order->quantity < 1) || (order->quantity > MAX_QTY)) {
        // printf("Trader error: order quantity invalid\n");
        return -1;
    }
    if ((order->price < 1) || (order->price > MAX_PRICE)) {
        // printf("Trader error: order price invalid\n");
        return -1;
    }
    // milestone fail condition
    if (order->quantity >= 1000) {
        return -1;
    }
    return 1;
}

int order_to_msg(char *msg, pe_order order) {
    memset(msg, '\0', BUFFER_SIZE);
    char next_type[5];
    if (strcmp(order.type, "BUY") == 0) {
        strcpy(next_type, "SELL");
    } else if (strcmp(order.type, "SELL") == 0) {
        strcpy(next_type, "BUY");
    } else {
        return -1;
    }
    sprintf(msg, "%s %d %s %d %d;",
            next_type,
            next_order_id(),
            order.product_name,
            order.quantity,
            order.price);
    return 1;
}

int order_accepted(char *msg) {
    char response[BUFFER_SIZE];
    int order_id;
    int n = sscanf(msg, "%s %d;", response, &order_id);
    if (n == 2) {
        if ((strcmp(response, "ACCEPTED") == 0) ||
            (strcmp(response, "AMENDED") == 0) ||
            (strcmp(response, "CANCELLED") == 0)) {
            return order_id;
        }
    } else if ((n == 1) && (strcpy(response, "INVALID") == 0)) {
        return -1;
    }
    printf("Trader error: caught invalid response\n");
    return -2;
}

void signal_handler(int sig) {}

int next_order_id() {
    return order_id_counter++;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Trader error: not enough arguments\n");
        return 1;
    }

    // register signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGINT);

    // connect to named pipes
    int trader_id = atoi(argv[1]);

    int exchange_fd = open_pipe(trader_id, FIFO_EXCHANGE, O_RDONLY);
    int trader_fd = open_pipe(trader_id, FIFO_TRADER, O_WRONLY);

    if (exchange_fd == -1 || trader_fd == -1) {
        printf("Trader error opening file descriptors\n");
        return -1;
    }

    char msg[BUFFER_SIZE];
    pe_order order;

    // read open market message
    if (read_pipe(&exchange_fd, msg) == -1) {
        printf("Trader error: failed to read market opening message");
        return -1;
    };

    // event loop:
    while (1) {
        // wait for exchange update (MARKET message)
        sigsuspend(&mask);
        
        if (read_pipe(&exchange_fd, msg) == -1) {
            printf("Trader error: failed to read message from exchange\n");
            break;
        };

        if (is_valid_order(msg) == -1) {
            continue;
        }

        // parse buffer into msg struct
        if (process_order(msg, &order) == -1) {
            break;
        };

        if (order_to_msg(msg, order) == -1) {
            printf("Trader error: parsing order to message\n");
            break;
        }

        // send order
        if (write_pipe(&trader_fd, msg) == -1) {
            printf("Trader error: failed to write to exchange\n");
            break;
        };

        // wait for exchange confirmation (ACCEPTED message)
        sigsuspend(&mask);
        if (read_pipe(&exchange_fd, msg) == -1) {
            printf("Trader error: failed to read confirmation message from exchange\n");
            break;
        };

        // check if order was accepted
        if (order_accepted(msg) < 0) {
            break;
        };
    }
    close(exchange_fd);
    close(trader_fd);
    return 0;
}