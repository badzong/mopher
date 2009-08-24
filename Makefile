MAKEFLAGS=-C build
CFLAGS=-Wall -DDEBUG -Iinclude -g
LDFLAGS=-rdynamic
CC=gcc
LEX=flex
YACC=bison -y
YFLAGS=-d

OBJECTS=main.o acl_yacc.o acl_lex.o acl.o milter.o modules.o ll.o ht.o var.o log.o cf.o parser.o hash.o util.o
GENSRC=acl_yacc.c acl_yacc.h acl_lex.c
LIBS=-lpthread -lmilter -ldl
DIRS=mod/acl

all: main $(DIRS)

.PHONY: clean
.PHONY: $(DIRS)

acl_yacc.c: acl_yacc.y
	$(YACC) $(YFLAGS) -p acl_ acl_yacc.y
	mv y.tab.c acl_yacc.c
	mv y.tab.h acl_yacc.h

acl_lex.c: acl_lex.l
	$(LEX) -t -Pacl_ acl_lex.l > acl_lex.c

../lib/lib.a:
	cd ../lib && $(MAKE)

main: Makefile $(OBJECTS) $(LIBS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o main $(OBJECTS) $(LIBS)

%.o: %.c include/%.h Makefile
	$(CC) $(CFLAGS) -c $<

$(DIRS):
	cd $@ && $(MAKE)

clean:
	-rm main $(OBJECTS) $(GENSRC)
	for d in $(DIRS); do cd $$d && $(MAKE) clean; done
