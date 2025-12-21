CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2
LDFLAGS = -pthread

.PHONY: all clean

all: server client

server: server.c common.h
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

clean:
	rm -f server client
