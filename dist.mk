# dist.mk

DOCSRC=			AUTHORS LICENSE README.md TODO doc
SRC=			$(DOCSRC) src config configure \
			Makefile.common.in Makefile.in config.h.in
OUT=			configure Makefile Makefile.common src/Makefile \
			src/modules/Makefile config.h autom4te.cache
REGDOM=			regdom_rules.dat regdom_rules.py

DIST?=			mopher
DISTDIR=		$(DIST)
ARCHIVE=		$(DIST).tar
TARBALL=		$(ARCHIVE).gz

all: bootstrap

bootstrap: src/regdom_rules.c
	aclocal
	autoconf

dist:
	mkdir $(DISTDIR)
	cp -PRp $(SRC) $(DISTDIR)
	tar cf $(ARCHIVE) $(DISTDIR)
	gzip $(ARCHIVE)

regdom_rules.dat:
	p=http://hg.mozilla.org/mozilla-central/raw-file/tip/netwerk/dns; \
	wget -O- $$p/effective_tld_names.dat >$@

regdom_rules.py:
	p=http://hg.mozilla.org/mozilla-central/raw-file/tip/netwerk/dns; \
	wget -O- $$p/prepare_tlds.py >$@

src/regdom_rules.c: regdom_rules.py regdom_rules.dat
	echo "/* Generated file. See dist.mk for details */" >$@
	python2.7 $> $^ >>$@

clean:
	-rm -rf $(OUT) $(DISTDIR) $(ARCHIVE) $(TARBALL) $(REGDOM)
