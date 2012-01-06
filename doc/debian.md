How-to compile mopher on debian linux
=====================================


Prerequisites
-------------

To build mopher you need a few basic compiler tools and libmilter. Here's the
detailed list of packages you need to install:

  * gcc
  * make
  * flex
  * bison
  * libmilter-dev
  * git-core (optional)
  * libspf2-dev (optional)
  * libdb4.8-dev (optional: BerkeleyDB support)
  * libmysqlclient-dev (optional: MySQL support)

```
# apt-get install gcc make flex bison libmilter-dev git-core libspf2-dev libdb4.8-dev libmysqlclient-dev
```


Fix missing Libmilter symlink
-----------------------------

I have no clue why the libmilter.so symlink is missing in the debian package.
Maybe I'll ask sometime. For now let's create the link manually.

```
# cd /usr/lib
# ln libmilter.so.1.0.1 libmilter.so
```


Compile mopher
--------------

You probably already downloaded mopher, otherwise:

```
# git clone https://github.com/badzong/mopher.git
# cd mopher
```


Regular build procedure:

```
# ./configure
# make
# make install
```


Add mopherd system user
-----------------------

```
# useradd -r mopherd
```

Configure Postfix
-----------------

If you're running postfix add the following line to your main.cf. This example
assumes, taht you're running smtpd chrooted.

```
smtpd_milters = unix:mopherd/mopherd.sock
```

Then create a seperate folder for your mopherd socket and database files.

```
# mkdir /var/spool/postfix/mopherd
# chown mopherd:postfix /var/spool/postfix/mopherd
# chmod 750 /var/spool/postfix/mopherd
```


Configuration
-------------

### mopherd.conf

Here's a basic configuration file using BerkeleyDB tables.

> /usr/local/etc/mopher/mopherd.conf

```
mopherd_user    = "mopherd"
mopherd_group   = "postfix"

milter_socket   = "unix:/var/spool/postfix/mopherd/mopherd.sock"

table[greylist] = {
    driver      = "bdb",
    path        = "/var/spool/postfix/mopherd/greylist.bdb"
}

table[counter_relay] = {
    driver      = "bdb",
    path        = "/var/spool/postfix/mopherd/counter_relay.bdb"
}

table[counter_penpal] = {
    driver      = "bdb",
    path        = "/var/spool/postfix/mopherd/counter_penpal.bdb"
}
```


### mail.acl

See doc/mail.acl.md
