CC = gcc
CFLAGS = -Wall

all: aloe

clean:
	rm -f aloe *.o

aloe: aloe.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $^
