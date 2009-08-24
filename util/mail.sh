RCPT=manuel@fredastaire.ch
FIFO=mail.fifo

if [ ! -z "$1" ]; then
	RCPT=$1
fi

if [ ! -e "$FIFO" ]; then
	mkfifo $FIFO
fi

if [ ! -p "$FIFO" ]; then
	echo $FIFO is not a FIFO
	exit 255
fi

sudo chmod 777 /var/spool/postfix/milter.sock

(
	echo "helo localhost"
	read
	echo "mail from: <test@milter>"
	read
	echo "rcpt to: <$RCPT>"
	read
    echo "data"
    read
    echo "Subject: MAIL.SH"
    echo 
    echo "MAIL.SH"
    echo "."
    read
	echo "quit"
	read
) < $FIFO | netcat localhost 25 | tee $FIFO

rm $FIFO
