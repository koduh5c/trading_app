/**
 * comp2017 - assignment 3
 * Min Kim
 * mkim5605
 */

#include "pe_exchange.h"

volatile int disconnected_traders;
pe_trader *traders;
pe_product *products;
int trader_count;
int product_count;
pe_trader *current_trader;
int exchange_fee;
pid_t parent_pid;
tracker *head;
tracker *tail;

int parse_products(char *product_fn) {
    FILE *fp = fopen(product_fn, "r");
    if (fp == 0) {
        printf("Error: could not open file\n");
        return -1;
    }
    char buffer[128];
    fgets(buffer, sizeof(buffer), fp);
    if (sscanf(buffer, "%d", &product_count) != 1) {
        printf("Error: first line format is invalid\n");
        return -1;
    }
    if (product_count < 1) {
        printf("Error: could not retrieve line count\n");
        return -1;
    }
    printf("%s Trading %d products: ", LOG_PREFIX, product_count);
    products = malloc(product_count * sizeof(pe_product));
    for (int i = 0; i < product_count; i++) {
        pe_product *p = &products[i];
        if (fgets(p->p_name, sizeof(p->p_name), fp) == NULL) {
            printf("Error: nothing to print from line %d\n", i);
            free(products);
            return -1;
        }
        p->p_name[strcspn(p->p_name, "\r\n")] = 0;
        p->buy_level = NULL;
        p->buy_levels = 0;
        p->sell_level = NULL;
        p->sell_levels = 0;
        printf("%s", products[i].p_name);
        if (i < product_count - 1) {
            printf(" ");
        }
    }
    printf("\n");
    fclose(fp);
    return 1;
}

int parse_traders(int argc, char **argv) {
    trader_count = argc - 2;
    traders = malloc(sizeof(pe_trader) * trader_count);
    for (int i = 0; i < trader_count; i++) {
        pe_trader *trader = &traders[i];
        trader->id = i;
        trader->disconnected = -1;
        trader->order_count = 0;
        sprintf(trader->bin_fn, "%s", argv[i + 2]);
        if (create_fifo(trader, FIFO_EXCHANGE) == -1) {
            return -1;
        }
        if (create_fifo(trader, FIFO_TRADER) == -1) {
            return -1;
        }
        if (connect_fifo(trader) == -1) {
            return -1;
        }
        trader->positions = malloc(sizeof(pe_position) * product_count);
        for (int j = 0; j < product_count; j++) {
            pe_position *p = &trader->positions[j];
            strcpy(p->p_name, products[j].p_name);
            p->traded_price = 0;
            p->traded_quantity = 0;
        }
        add_tracker(trader->positions);
    }
    return 1;
}

void add_tracker(void *data) {
    tracker *new_tracker = malloc(sizeof(tracker));
    new_tracker->data = data;
    new_tracker->next = NULL;
    if (head == NULL) {
        head = new_tracker;
        tail = new_tracker;
    } else {
        tail->next = new_tracker;
        tail = tail->next;
    }
}

void remove_tracker(void *data) {
    tracker *curr = head;
    tracker *prev = NULL;
    while (curr != NULL) {
        if (curr->data == data) {
            if (prev == NULL) {
                head = curr->next;
                if (head == NULL) {
                    tail = NULL;
                }
            } else {
                prev->next = curr->next;
                if (curr->next == NULL) {
                    tail = prev;
                }
            }
            free(curr->data);
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

int read_pipe(int *fd, char *buffer) {
    memset(buffer, '\0', BUFFER_SIZE);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (read(*fd, buffer + i, 1) == -1) {
            return -1;
        };
        if (buffer[i] == ';') {
            break;
        }
    }
    buffer[strlen(buffer)] = '\0';
    return 1;
}

int write_pipe(int *fd, char *buffer) {
    write(*fd, buffer, strlen(buffer));
    // if (n == -1) {
    //     printf("Error: failed to write order to pipe\n");
    //     return -1;
    // } else if (n == 0) {
    //     printf("Error: nothing was written to the pipe\n");
    //     return -1;
    // }
    return 1;
}

int create_fifo(pe_trader *trader, char *fifo_format) {
    char *fifo_fn = malloc((strlen(fifo_format) + 1) * sizeof(char));
    sprintf(fifo_fn, fifo_format, trader->id);
    if (strcmp(fifo_format, FIFO_EXCHANGE) == 0) {
        sprintf(trader->write_fn, "%s", fifo_fn);
    } else if (strcmp(fifo_format, FIFO_TRADER) == 0) {
        sprintf(trader->read_fn, "%s", fifo_fn);
    } else {
        printf("Error: invalid fifo format given\n");
        free(fifo_fn);
        return -1;
    }
    if (access(fifo_fn, F_OK) == 0) {
        unlink(fifo_fn);
    }
    if (mkfifo(fifo_fn, 0777) == -1) {
        printf("Error: failed to create %s file\n", fifo_fn);
        free(fifo_fn);
        return -1;
    }
    printf("%s Created FIFO %s\n", LOG_PREFIX, fifo_fn);
    free(fifo_fn);
    return 1;
}

int connect_fifo(pe_trader *trader) {
    char nchar[10];
    sprintf(nchar, "%d", trader->id);
    printf("%s Starting trader %d (%s)\n", LOG_PREFIX, trader->id, trader->bin_fn);
    trader->pid = fork();
    if (trader->pid == -1) {
        printf("Error: failed to fork for %s\n", trader->bin_fn);
        return -1;
    }
    if (trader->pid == 0) {  // child process
        execl(trader->bin_fn, trader->bin_fn, nchar, NULL);
    } else {  // parent process
        trader->write_fd = open(trader->write_fn, O_WRONLY);
        if (trader->write_fd == -1) {
            printf("Error: opening file descriptors for %s\n", trader->bin_fn);
            return -1;
        }
        printf("%s Connected to %s\n", LOG_PREFIX, trader->write_fn);

        trader->read_fd = open(trader->read_fn, O_RDONLY);
        if (trader->read_fd == -1) {
            printf("Error: opening file descriptors for %s\n", trader->bin_fn);
            return -1;
        }
        printf("%s Connected to %s\n", LOG_PREFIX, trader->read_fn);

        if (write_pipe(&trader->write_fd, "MARKET OPEN;") == -1) {
            printf("Error: failed to write MARKET OPEN to trader %d\n", trader->id);
            return -1;
        }
        kill(trader->pid, SIGUSR1);
    }
    return 1;
}

pe_level *create_level(pe_order *new_order) {
    pe_level *l = malloc(sizeof(pe_level));
    l->price = new_order->price;
    l->quantity = new_order->quantity;
    l->next = NULL;
    l->orders = new_order;
    l->size = 1;
    add_tracker(l);
    return l;
}

int add_order(pe_product *product, pe_order *new_order) {
    if (new_order->quantity <= 0) {
        return -1;
    }
    pe_level **level_list = NULL;
    int *level_count;
    if (strcmp(new_order->command, "BUY") == 0) {
        level_list = &product->buy_level;
        level_count = &product->buy_levels;
    } else if (strcmp(new_order->command, "SELL") == 0) {
        level_list = &product->sell_level;
        level_count = &product->sell_levels;
    } else {
        printf("Error: invalid command while adding level/order\n");
        return -1;
    }
    if (*level_list == NULL) {
        *level_list = create_level(new_order);
        *level_count = *level_count + 1;
        return 1;
    }
    pe_level *curr_level = *level_list;
    pe_level *prev_level = NULL;
    while (curr_level != NULL) {
        if (curr_level->price == new_order->price) {
            pe_order *curr_order = curr_level->orders;
            while (curr_order->next != NULL) {
                curr_order = curr_order->next;
            }
            curr_order->next = new_order;
            curr_level->quantity = curr_level->quantity + curr_order->next->quantity;
            curr_level->size = curr_level->size + 1;
            return 1;
        }
        if (prev_level == NULL) {
            if (new_order->price > curr_level->price) {
                pe_level *new_level = create_level(new_order);
                new_level->next = curr_level;
                *level_list = new_level;
                *level_count = *level_count + 1;
                return 1;
            }
        } else {
            if (prev_level->price > new_order->price && new_order->price > curr_level->price) {
                pe_level *new_level = create_level(new_order);
                new_level->next = curr_level;
                prev_level->next = new_level;
                *level_count = *level_count + 1;
                return 1;
            }
        }
        prev_level = curr_level;
        curr_level = curr_level->next;
    }
    curr_level = create_level(new_order);
    prev_level->next = curr_level;
    *level_count = *level_count + 1;
    return 1;
}

int remove_order(pe_order *order, int is_amend) {
    for (int i = 0; i < product_count; i++) {
        pe_product *p = &products[i];
        for (int j = 0; j < 2; j++) {
            pe_level **level_list = j == 0 ? &p->buy_level : &p->sell_level;
            int *level_count = j == 0 ? &p->buy_levels : &p->sell_levels;
            pe_level *current_level = *level_list;
            pe_level *prev_level = NULL;
            int removed = -1;
            while (current_level != NULL) {
                pe_order *current_order = current_level->orders;
                pe_order *prev_order = NULL;
                while (current_order != NULL) {
                    if (current_order->trader == current_trader && current_order->id == order->id) {
                        strcpy(order->command, current_order->command);
                        strcpy(order->p_name, current_order->p_name);
                        order->trader = current_order->trader;
                        pe_order *tmp = current_order;
                        if (prev_order == NULL) {
                            current_level->orders = current_order->next;
                        } else {
                            prev_order->next = current_order->next;
                        }
                        if (is_amend == 1) {
                            tmp->quantity = order->quantity;
                            tmp->price = order->price;
                        } else {
                            tmp->quantity = 0;
                            tmp->price = 0;
                        }
                        notify_traders(tmp);
                        remove_tracker(tmp);
                        current_level->size--;
                        removed = 1;
                        break;
                    } else {
                        prev_order = current_order;
                        current_order = current_order->next;
                    }
                }
                // remove level if no more orders
                if (removed == 1) {
                    if (current_level->size <= 0) {
                        pe_level *tmp = current_level;
                        if (prev_level == NULL) {
                            *level_list = current_level->next;
                        } else {
                            prev_level->next = current_level->next;
                        }
                        remove_tracker(tmp);
                        *level_count = *level_count - 1;
                    }
                    return 1;
                } else {
                    prev_level = current_level;
                    current_level = current_level->next;
                }
            }
        }
    }
    return -1;
}

void match_orders(pe_product *product, pe_order *new_order) {
    pe_level **level_list = NULL;
    int *level_count;
    if (strcmp(new_order->command, "BUY") == 0) {
        level_list = &product->sell_level;
        level_count = &product->sell_levels;
    } else if (strcmp(new_order->command, "SELL") == 0) {
        level_list = &product->buy_level;
        level_count = &product->buy_levels;
    }
    if (level_list == NULL) {
        return;
    }
    pe_level *current_level = *level_list;
    pe_level *prev_level = NULL;
    while (current_level != NULL) {
        if (new_order->quantity <= 0) {
            break;
        }

        if ((strcmp(new_order->command, "SELL") == 0 && new_order->price <= current_level->price) ||
            (strcmp(new_order->command, "BUY") == 0 && new_order->price >= current_level->price)) {
            pe_order *current_order = current_level->orders;
            pe_order *prev_order = NULL;
            while (current_order != NULL) {
                // break if new order has no more to trade
                if (new_order->quantity <= 0) {
                    break;
                }

                // calculate quantity able to trade
                long traded_quantity;
                if (current_order->quantity - new_order->quantity >= 0) {
                    traded_quantity = new_order->quantity;
                } else {
                    traded_quantity = current_order->quantity;
                }

                // calculate value and fee
                long value = traded_quantity * current_order->price;
                long fee = round(traded_quantity * (double)current_order->price * FEE_PERCENTAGE / 100);

                // add fee to global
                exchange_fee += fee;

                // update buyer and seller position price and quantity
                int modifier = strcmp(new_order->command, "BUY") == 0 ? 1 : -1;

                pe_position *new_pos = NULL;
                pe_position *old_pos = NULL;

                for (int i = 0; i < product_count; i++) {
                    if (strcmp(new_order->trader->positions[i].p_name, new_order->p_name) == 0) {
                        new_pos = &new_order->trader->positions[i];
                    }
                    if (strcmp(current_order->trader->positions[i].p_name, current_order->p_name) == 0) {
                        old_pos = &current_order->trader->positions[i];
                    }
                }
                new_pos->traded_price -= value * modifier + fee;
                new_pos->traded_quantity += traded_quantity * modifier;

                old_pos->traded_price += value * modifier;
                old_pos->traded_quantity -= traded_quantity * modifier;

                printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n",
                       LOG_PREFIX,
                       current_order->id,
                       current_order->trader->id,
                       new_order->id,
                       new_order->trader->id,
                       value,
                       fee);

                // respond to relevant traders order was filled
                respond_order_filled(current_order, new_order, traded_quantity);

                // deduct quantity from order and level
                if (current_order->quantity - new_order->quantity < 0) {
                    new_order->quantity -= current_order->quantity;
                    current_level->quantity -= current_order->quantity;
                    current_order->quantity = 0;
                } else {
                    current_order->quantity -= new_order->quantity;
                    current_level->quantity -= new_order->quantity;
                    new_order->quantity = 0;
                }

                // remove order from level if no more quantity
                if (current_order->quantity <= 0) {
                    pe_order *tmp = current_order;
                    if (prev_order == NULL) {
                        current_level->orders = current_order->next;
                    } else {
                        prev_order->next = current_order->next;
                    }
                    current_order = current_order->next;
                    remove_tracker(tmp);
                    current_level->size--;
                } else {
                    prev_order = current_order;
                    current_order = current_order->next;
                }
            }
        }
        // remove level if no more orders
        if (current_level->quantity <= 0) {
            pe_level *tmp = current_level;
            if (prev_level == NULL) {
                *level_list = current_level->next;
            } else {
                prev_level->next = current_level->next;
            }
            current_level = current_level->next;
            remove_tracker(tmp);
            *level_count = *level_count - 1;
        } else {
            prev_level = current_level;
            current_level = current_level->next;
        }
    }
}

void report() {
    printf("%s\t--ORDERBOOK--\n", LOG_PREFIX);
    for (int i = 0; i < product_count; i++) {
        pe_product *p = &products[i];
        printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n",
               LOG_PREFIX,
               p->p_name,
               p->buy_levels,
               p->sell_levels);
        pe_level *c = p->sell_level;
        while (c != NULL) {
            printf("%s\t\tSELL %ld @ $%ld (%d %s)\n",
                   LOG_PREFIX,
                   c->quantity,
                   c->price,
                   c->size,
                   c->size > 1 ? "orders" : "order");
            c = c->next;
        }
        c = p->buy_level;
        while (c != NULL) {
            printf("%s\t\tBUY %ld @ $%ld (%d %s)\n",
                   LOG_PREFIX,
                   c->quantity,
                   c->price,
                   c->size,
                   c->size > 1 ? "orders" : "order");
            c = c->next;
        }
    }
    printf("%s\t--POSITIONS--\n", LOG_PREFIX);
    for (int i = 0; i < trader_count; i++) {
        pe_trader *t = &traders[i];
        printf("%s\tTrader %d:", LOG_PREFIX, t->id);
        for (int j = 0; j < product_count; j++) {
            pe_position *pos = &t->positions[j];
            printf(" %s %ld ($%ld)", pos->p_name, pos->traded_quantity, pos->traded_price);
            if (j < product_count - 1) {
                printf(",");
            }
        }
        printf("\n");
    }
}

void notify_traders(pe_order *order) {
    char response[BUFFER_SIZE];
    sprintf(response, "MARKET %s %s %ld %ld;",
            order->command,
            order->p_name,
            order->quantity,
            order->price);
    for (int i = 0; i < trader_count; i++) {
        if (current_trader->pid != traders[i].pid && traders[i].disconnected == -1) {
            write_pipe(&traders[i].write_fd, response);
            kill(traders[i].pid, SIGUSR1);
        }
    }
}

void respond_order_filled(pe_order *order_a, pe_order *order_b, int filled_quantity) {
    char response_a[BUFFER_SIZE];
    sprintf(response_a, "FILL %d %d;",
            order_a->id,
            filled_quantity);

    char response_b[BUFFER_SIZE];
    sprintf(response_b, "FILL %d %d;",
            order_b->id,
            filled_quantity);

    int response_count = 0;
    for (int i = 0; i < trader_count; i++) {
        if (response_count == 2) {
            break;
        }
        if (order_a->trader == &traders[i] && order_a->trader->disconnected == -1) {
            if (write_pipe(&order_a->trader->write_fd, response_a) == -1) {
                printf("Error: failed to send fill response to trader %d\n", order_a->trader->id);
                return;
            }
            kill(order_a->trader->pid, SIGUSR1);
            response_count++;
        } else if (order_b->trader == &traders[i] && order_b->trader->disconnected == -1) {
            if (write_pipe(&order_b->trader->write_fd, response_b) == -1) {
                printf("Error: failed to send fill response to trader %d\n", order_b->trader->id);
                return;
            }
            kill(order_b->trader->pid, SIGUSR1);
            response_count++;
        }
    }
}

void respond_to_trader(char *command, pe_order *order) {
    char response[BUFFER_SIZE];
    if (strcmp(command, "ACCEPTED") == 0) {
        sprintf(response, "ACCEPTED %d;", order->id);
    } else if (strcmp(command, "AMENDED") == 0) {
        sprintf(response, "AMENDED %d;", order->id);
    } else if (strcmp(command, "CANCELLED") == 0) {
        sprintf(response, "CANCELLED %d;", order->id);
    } else if (strcmp(command, "INVALID") == 0) {
        strcpy(response, "INVALID;");
    } else {
        printf("Error: invalid response command\n");
        return;
    }
    write_pipe(&current_trader->write_fd, response);
    kill(current_trader->pid, SIGUSR1);
}

int validate_new_order(pe_order *new_order) {
    if ((new_order->id < 0) || (new_order->id > MAX_ORDER_ID)) {
        return -1;
    }
    if ((new_order->quantity < 1) || (new_order->quantity > MAX_QTY)) {
        return -1;
    }
    if ((new_order->price < 1) || (new_order->price > MAX_PRICE)) {
        return -1;
    }

    return 1;
}

pe_product *get_product_by_name(char *p_name) {
    pe_product *p = NULL;
    for (int i = 0; i < product_count; i++) {
        if (strcmp(products[i].p_name, p_name) == 0) {
            p = &products[i];
            break;
        }
    }
    return p;
}

int process_order(char *buffer, pe_order *new_order) {
    if (strlen(buffer) < 1) {
        return -1;
    }
    buffer[strlen(buffer) - 1] = '\0';
    printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, current_trader->id, buffer);

    if (sscanf(buffer, "%s", new_order->command) != 1) {
        return -1;
    }

    if (strcmp(new_order->command, "AMEND") == 0) {
        if (sscanf(buffer, "%*s %d %ld %ld", &new_order->id, &new_order->quantity, &new_order->price) != 3) {
            return -1;
        }
        if (new_order->id >= current_trader->order_count) {
            return -1;
        }
        if (validate_new_order(new_order) == -1) {
            return -1;
        }
        if (remove_order(new_order, 1) == -1) {
            return -1;
        }

        pe_product *p = get_product_by_name(new_order->p_name);
        add_order(p, new_order);
        respond_to_trader("AMENDED", new_order);
        add_tracker(new_order);
        report();
        return 1;
    } else if (strcmp(new_order->command, "CANCEL") == 0) {
        if (sscanf(buffer, "%*s %d", &new_order->id) != 1) {
            return -1;
        }
        if (new_order->id >= current_trader->order_count) {
            return -1;
        }
        if (remove_order(new_order, -1) == -1) {
            return -1;
        }
        respond_to_trader("CANCELLED", new_order);
        report();
        free(new_order);
        return 1;
    } else if (strcmp(new_order->command, "BUY") == 0 || strcmp(new_order->command, "SELL") == 0) {
        if (sscanf(buffer, "%*s %d %s %ld %ld",
                   &new_order->id,
                   new_order->p_name,
                   &new_order->quantity,
                   &new_order->price) != 4) {
            return -1;
        }
        if (new_order->id != current_trader->order_count) {
            return -1;
        }
        new_order->next = NULL;
        new_order->trader = current_trader;
        if (validate_new_order(new_order) == -1) {
            return -1;
        }
        pe_product *p = get_product_by_name(new_order->p_name);
        if (p == NULL) {
            return -1;
        }
        respond_to_trader("ACCEPTED", new_order);
        notify_traders(new_order);
        match_orders(p, new_order);
        if (add_order(p, new_order) == 1) {
            add_tracker(new_order);
        } else {
            free(new_order);
        }
        report();
        current_trader->order_count++;
        return 1;
    }
    return -1;
}

void sigusr1_handler(int signum, siginfo_t *info, void *context) {
    for (int i = 0; i < trader_count; i++) {
        if (info->si_pid == traders[i].pid) {
            current_trader = &traders[i];
            break;
        }
    }
}

void sigchld_handler(int signum, siginfo_t *info, void *context) {
    for (int i = 0; i < trader_count; i++) {
        if (traders[i].pid == info->si_pid) {
            close(traders[i].write_fd);
            unlink(traders[i].write_fn);
            close(traders[i].read_fd);
            unlink(traders[i].read_fn);
            printf("%s Trader %d disconnected\n", LOG_PREFIX, traders[i].id);
            traders[i].disconnected = 1;
            disconnected_traders++;
            break;
        }
    }
}

void free_trackers() {
    if (head != NULL) {
        tracker *curr = head;
        tracker *tmp = NULL;
        while (curr != NULL) {
            tmp = curr->next;
            free(curr->data);
            free(curr);
            curr = tmp;
        }
    }
    free(traders);
    free(products);
}

void init_globals() {
    head = NULL;
    tail = NULL;
    exchange_fee = 0;
    disconnected_traders = 0;
    parent_pid = getpid();
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Error: not enough arguments\n");
        return -1;
    }

    init_globals();

    struct sigaction sa_usr1;
    sa_usr1.sa_sigaction = sigusr1_handler;
    sa_usr1.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa_usr1, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_sigaction = sigchld_handler;
    sa_chld.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &sa_chld, NULL);

    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    printf("%s Starting\n", LOG_PREFIX);

    if (parse_products(argv[1]) == -1) {
        printf("Error: failed parsing products\n");
        return -1;
    }

    if (parse_traders(argc, argv) == -1) {
        printf("Error: failed parsing traders\n");
        return -1;
    }

    char buffer[BUFFER_SIZE];

    while (disconnected_traders != trader_count) {
        // sigprocmask(SIG_UNBLOCK, &mask, NULL);
        sigsuspend(&mask);
        // sigprocmask(SIG_BLOCK, &mask, NULL);
        if (disconnected_traders == trader_count) {
            break;
        }
        if (current_trader->disconnected == -1) {
            read_pipe(&current_trader->read_fd, buffer);
            pe_order *order = malloc(sizeof(pe_order));
            if (process_order(buffer, order) == -1) {
                respond_to_trader("INVALID", NULL);
                free(order);
            }
        }
    }
    printf("%s Trading completed\n", LOG_PREFIX);
    printf("%s Exchange fees collected: $%d\n", LOG_PREFIX, exchange_fee);
    free_trackers();
    return 0;
    // finish
}