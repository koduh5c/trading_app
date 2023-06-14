CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS=-lm
BINARIES=pe_exchange pe_trader

all: $(BINARIES)

.PHONY: clean
clean:
	rm -f $(BINARIES)

push:
	make clean
	make
	git add .
	git commit -m '.'
	git push
	make clean

run:
	make clean
	make
	./pe_exchange
	make clean