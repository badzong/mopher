RCPT=manuel@fredastaire.ch
FROM=manuel@badzong.com
FIFO=mail.fifo
HOST=127.0.0.1

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
	echo "check this command!"
	read
	echo "mail from: <$FROM>"
	read
	echo "rcpt to: <$RCPT>"
	read
	echo "rcpt to: <orders@badzong.com>"
	read
	echo "rcpt to: <lists@badzong.com>"
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
) < $FIFO | netcat $HOST 25 | tee $FIFO

rm $FIFO
