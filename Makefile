CC=gcc
CFLAGS=-Wall -g
LDFLAGS=
LDLIBS=-lserialport

TARGET=epromburn

$(TARGET): main.o
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@
