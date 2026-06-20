CC = gcc
CFLAGS = -Wall -pthread -g -O2
LDFLAGS = -lrt

all: server client settlement

server: server.c engine.c common.h engine.h
	$(CC) $(CFLAGS) -o server server.c engine.c $(LDFLAGS)

client: client.c common.h
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS) -lncurses

settlement: settlement.c common.h
	$(CC) $(CFLAGS) -o settlement settlement.c $(LDFLAGS)

clean:
	rm -rf server client settlement data/
