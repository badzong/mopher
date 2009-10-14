CC=gcc
CFLAGS=-Wall -g -shared -nostartfiles -fPIC -I../../include
OBJECTS=bdb.so my.so

all: $(OBJECTS)

bdb.so: bdb.c Makefile
	$(CC) $(CFLAGS) -ldb -o $@ $<

my.so: my.c Makefile
	#$(CC) $(CFLAGS) -lmysqlclient -o $@ $<
	$(CC) $(CFLAGS) -o $@ $<

clean:
	-rm $(OBJECTS)
