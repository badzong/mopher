CC=gcc
CFLAGS=-Wall -shared -nostartfiles -fPIC -I../../include
OBJECTS=test.so string.so

all: $(OBJECTS)

%.so: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
