CC=gcc
CFLAGS=-Wall -shared -nostartfiles -fPIC -I../../include
OBJECTS=greylist.so

all: $(OBJECTS)

%.so: %.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
