# dist.mk

DOCSRC=			AUTHORS LICENSE README.md TODO doc
SRC=			$(DOCSRC) src config configure \
			Makefile.common.in Makefile.in config.h.in
OUT=			configure Makefile Makefile.common src/Makefile \
			src/modules/Makefile config.h autom4te.cache

DIST?=			mopher
DISTDIR=		$(DIST)
ARCHIVE=		$(DIST).tar
TARBALL=		$(ARCHIVE).gz

all: bootstrap dist

bootstrap:
	aclocal
	autoconf

dist:
	mkdir $(DISTDIR)
	cp -PRp $(SRC) $(DISTDIR)
	tar cf $(ARCHIVE) $(DISTDIR)
	gzip $(ARCHIVE)

clean:
	-rm -rf $(OUT) $(DISTDIR) $(ARCHIVE) $(TARBALL)
