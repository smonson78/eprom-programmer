CC=gcc
CFLAGS=-Wall -g
LDFLAGS=
LDLIBS=-lserialport

TARGET=epromburn

$(TARGET): main.o crc16.o
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	$(RM) *.o $(TARGET)
