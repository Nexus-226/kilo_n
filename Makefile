CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
LDLIBS = -lncurses

kilo_nc: kilo_nc.o
	$(CC) $^ -o $@ $(LDLIBS)

clean:
	rm -f *.o kilo_nc