RCPT=manuel@fredastaire.ch

if [ ! -z "$1" ]; then
	RCPT=$1
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
) < mail.fifo | netcat localhost 25 | tee mail.fifo
