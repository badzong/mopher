#!/bin/sh

# This script is used to setup my personal testing environment.
# You probably don't wanna use this.

FILES="mail.acl mopherd.conf"
CFGDIR=/etc/mopher
SRCDIR=/home/manuel/src/mopher
EXT=.copy

for file in $FILES; do
	sudo cp $CFGDIR/$file{,$EXT}
done

(
	cd $SRCDIR
	make clean
	make
	sudo make install
)

for file in $FILES; do
	sudo cp $CFGDIR/$file{$EXT,}
done

