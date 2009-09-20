CWD=/home/manuel/src
BACKUP=mopher
REMOVE="mopher/var/greylist.bdb"
HOST=morrison.andev.ch

echo backing up: $CMD/$BACKUP
cd $CWD

(cd $BACKUP && make clean)

for file in $REMOVE; do
	echo delete: $file
	rm $file
done

echo create archive
tar cjf $BACKUP.tar.bz2 $BACKUP

echo save archive on $HOST
scp $BACKUP.tar.bz2 $HOST:
