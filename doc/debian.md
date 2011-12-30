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
  * git-core (Optional)
  * libdb4.8-dev (Optional: BerkeleyDB support)
  * libmysqlclient-dev (Optional: MySQL support)

```
# apt-get install gcc make flex bison libmilter-dev libdb4.8-dev libmysqlclient-dev
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
# mkdir /var/run/mopherd
# chown mopherd /var/run/mopherd /usr/local/var/lib/mopher/db
```


Configuration
-------------

### mopherd.conf

Here's a basic configuration file using BerkeleyDB gerylist tables.

> /usr/local/etc/mopher/mopherd.conf

```
mopherd_user    = "mopherd"
mopherd_group   = "postfix"

milter_socket   = "/var/run/mopherd/mopherd.sock"

table[greylist] = {
    driver      = "bdb",
    path        = "/usr/local/var/lib/mopher/db/greylist.bdb"
}

#table[greylist] = {
#    driver      = "memdb"
#}
```


### mail.acl

Simple mail.acl using greylisting.

> /usr/local/etc/mopher/mail.acl

```
connect log milter_id + ":: new connection: hostname=" + milter_hostname + " (" + milter_hostaddr + ")"
connect tarpit 10s

envrcpt log milter_id + ":: envelope: from=" + string_mailaddr(milter_envfrom) + " rcpt=" + string_mailaddr(milter_envrcpt)
envrcpt greylist delay 5m attempts 3
    
eom log milter_id + ":: message: queue_id=" + milter_queueid + " message_size=" + (milter_header_size + milter_body_size)
eom tarpit_delayed add header "X-Tarpit" value "message tarpitted for " + tarpit_delayed + " seconds"
eom greylist_delayed add header "X-Greylist" value "message greylisted for " + greylist_delayed + " seconds" 

close log milter_id + ":: connection: closed"
```
