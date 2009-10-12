MAKEFLAGS=-C build
CFLAGS=-Wall -DDEBUG -Iinclude -g
LDFLAGS=-rdynamic
CC=gcc
LEX=flex
YACC=bison -y
YFLAGS=-d

OBJECTS=main.o cf_yacc.o cf_lex.o cf.o cf_defaults.o acl_yacc.o acl_lex.o acl.o milter.o modules.o ll.o ht.o var.o log.o hash.o util.o dbt.o greylist.o sock.o msync.o sql.o
GENSRC=cf_yacc.c cf_yacc.h cf_lex.c acl_yacc.c acl_yacc.h acl_lex.c
LIBS=-lpthread -lmilter -ldl -lmysqlclient
DIRS=mod/acl mod/db mod/tables

all: main $(DIRS)

.PHONY: clean
.PHONY: $(DIRS)

cf_defaults.o: cf_defaults.conf
	ld -r -b binary -o $@ $<
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@ $@

cf_yacc.c: cf_yacc.y
	$(YACC) $(YFLAGS) -p cf_ cf_yacc.y
	mv y.tab.c cf_yacc.c
	mv y.tab.h cf_yacc.h

cf_lex.c: cf_lex.l
	$(LEX) -t -Pcf_ cf_lex.l > cf_lex.c

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
	for d in $(DIRS); do (cd $$d && $(MAKE) clean); done
