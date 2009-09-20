CC=gcc
CFLAGS=-Wall -g -shared -nostartfiles -fPIC -I../../include
OBJECTS=bdb.so

all: $(OBJECTS)

%.so: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
