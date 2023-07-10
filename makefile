CC = gcc
CFLAGS = -DTHPOOL_DEBUG
LIBS = -pthread

gateway: gateway.o thpool.o
	$(CC) $(CFLAGS) $(LIBS) -o gateway gateway.o thpool.o

gateway.o: gateway.c
	$(CC) $(CFLAGS) -c gateway.c

thpool.o: thpool.c
	$(CC) $(CFLAGS) -c thpool.c

clean:
	rm -f gateway gateway.o thpool.o
