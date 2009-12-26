RCPT=manuel@fredastaire.ch
FROM=manuel@badzong.com
FIFO=mail.fifo_$$
HOST=62.112.216.20

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

(
	echo "helo localhost"
	read
	echo "check this command!"
	read
	echo "mail from: <$FROM>"
	read
	echo "rcpt to: <$RCPT>"
	read
	#echo "rcpt to: <orders@badzong.com>"
	#read
	#echo "rcpt to: <lists@badzong.com>"
	#read
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
