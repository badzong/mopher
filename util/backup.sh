CWD=/home/manuel/src
BACKUP=mopher

echo backing up: $CMD/$BACKUP
cd $CWD
tar cjf $BACKUP.tar.bz2 $BACKUP
scp $BACKUP.tar.bz2 morrison.andev.ch:
