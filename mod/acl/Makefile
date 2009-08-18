CC=gcc
CFLAGS=-shared -nostartfiles -fPIC -I../../../lib/include -I../../include
OBJECTS=test.so string.so

all: $(OBJECTS)

%.so: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
