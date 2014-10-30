# Makefile

SUBDIR+=	src/modules
SUBDIR+=	src
SUBDIR+=	man
SUBDIR+=	config

TARGETS+=	build
TARGETS+=	clean
TARGETS+=	install
TARGETS+=	uninstall

$(TARGETS):
	for i in $(SUBDIR); do \
		(cd $$i && $(MAKE) $@); done


include make/mk.admin
