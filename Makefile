CC=gcc
CFLAGS=-O -Wall -Wextra

dcc6502: dcc6502.c
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *.o dcc6502 dcc6502.exe

all: dcc6502