CC=gcc
CFLAGS=-Wall -Wextra -pedantic -o

qeditor: qeditor.c
	$(CC) $< $(CFLAGS) qeditor -std=c99