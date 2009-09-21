CC=gcc
CFLAGS=-Wall -g -shared -nostartfiles -fPIC -I../../include
OBJECTS=milter.so string.so spf.so

all: $(OBJECTS)

spf.so: spf.c
	$(CC) $(CFLAGS) -lspf2 -o $@ $<

%.so: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
