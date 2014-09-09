# NAME
mopherd.conf - mopherd configuration file syntax

# SYNOPSIS
**{{ MOPHERD_CONF }}**

# DESCRIPTION
**mopherd.conf** controls the behavior of the mopherd mail filter daemon.
Mopherd has a simple and solid builtin configuration and should run out of the
box without any configuration changes necessary. However some mopher features
need a little configuration.

## Main Configuration Directives

  * acl_log_level ([0-7], default: 3):
    Syslog log level for messages logged through mail.acl log directive. 0 is
    the highest, 7 the lowest log level, where lower means more messages.
    
  * acl_path (default: {{ MAIL_ACL }}):
    Path where mopherd looks for the mail access control list. The mail.acl
    controls how mopher handles incoming email.

  * client_retry_interval (default: 10)
    ( EXPERIMENTAL )

  * control_socket (default: inet:44554@127.0.0.1)
    The control socket is used by moco to control a running mopherd instance.
    Supported socket types are inet sockets (TCP/SOCK_STREAM) and unix domain
    sockets.

  * dbt_cleanup_interval (seconds, default: 600):
    Most mopher tables need regular clean up. Depending on the used database
    backend this may require a full table lock and block all email delivery
    until the table is clean. Do not bother to change this setting unless your
    server handels hundreds of connections per second.

  * foreground ([0|1], default: 0)
    If set to 1 mopherd does not fork into background. Mostly useful for
    debugging.

  * greylist_deadline (seconds, default: 86400)
    Default greylist deadline if not given in mail.acl. If deadline has passed
    the greylist record is deleted from the greylist table.

  * greylist_visa (seconds, default: 2592000)
    Default greylist visa time if not given in mail.acl. If a relay passes
    greylisting once it will no longer be greylisted. Every time a relay sends
    an email, the visa is renewd.

  * hostname (default: gethostname(3))
    Hostname used in self references. E.g. for the fake spamd received header.
    
  * log_level (default: 4)
    Log verbosity from 0 lowest to 7 highest. See man syslog(3) for details.

  * milter_socket (default: inet:44555@127.0.0.1)
    The milter socket is used by your MTA e.g. Postfix or Sendmail to connect
    to mopherd. Supported socket types are inet sockets (TCP/SOCK_STREAM) and
    unix domain sockets.

  * milter_socket_timeout (seconds, default: -1)
    The number of seconds for libmilter to wait before timing out.

  * milter_socket_permissions (octal permissions, default: 666)
    UNIX filesystem permissions for the milter socket. Only applies to unix
    domain sockets.

  * milter_wait (seconds, default: 1)
    Number of seconds mopherd waits for all milter threads to close upon exit.

  * mopherd_group (default: mopherd)
    If mopherd is running with group root it switches to this group for
    security reasons. Files and sockets are created with this group.

  * mopherd_user (default: mopherd)
    If mopherd is running as user root it switches to this user for
    security reasons. Files and sockets are created as this user.

  * module_path (default: {{ MODULE_PATH}})
    Directory where mopherd looks for modules. All modules in this directory
    are automatically loaded on startup.

  * tarpit_progress_interval (seconds, default: 10)
    Number of seconds tarpit repeatedly logs a debug message while tarpitting.

  * workdir_path (default: /)
    Mopherd changes the current working directory to workdir_path on startup.


## Database configuration

Mopher uses database tables to store information. At this moment mopher has
these four tables:

  * state
    The state table saves the mopherd state. Currently the only value stored
    in this database is the milter_id. This is handy for unique email
    identification in logs.

  * greylist
    The greylist database.

  * counter_relay
    The counter module counts every delivered email for every relay. 
    
  * counter_penpal
    The counter module counts every delivered email for every sender and
    recipient address combination.

The compiletime default driver for this tables is memdb. Stored information
will be lost on proccess termination. To persist the tables filesystem based
drivers have to be used.


### Supported databases

  * memdb
    A hashtable stored only in memory. All contents are lost when mopher exits.

  * bdb (Berkeley DB)
    High performance embedded database system. 

  * sakila (MySQL)
    Widely used SQL database management system. The name sakila was chosen, to
    maintain a clean namespace.


### Database examples

table[state] = {
    "driver" = "bdb",
    "path"   = "/var/local/lib/mopher/state.bdb"
}
    
table[greylist] = {
    "driver"   = "sakila",
    "host"     = "192.168.1.100",
    "database" = "mopher",
    "table"    = "mopher_greylist",
    "user"     = "mopherd",
    "pass"     = "secret"
}
    

## Modules

### Counter

The counter module counts accepted and delivered mails and keeps track of
relays and penpals. The module can provide a shortcut through your mail.acl
for regularly seen mail relays or sender recipient address pairs.

  * counter_expire_low (seconds, default: 604800)
    Number of seconds counter records are kept in the database with counter
    value lower than counter_threshold.

  * counter_expire_high (seconds, default: 5184000)
    Number of seconds counter records are kept in the database with counter
    value higher than counter_threshold.

  * counter_threshold (default: 3)
    Number of successful delivered emails to switch from low to high.


### Realtime Blacklists (RBL)

To use RBLs in your mail.acl, you need to configure them in your mopherd.conf.
All RBL symbols are configured in the *rbl* configuration table. For example if
you want to use your own RBL named rbl_example from blacklist.example.org.

rbl[rbl_example] = "blacklist.example.org"


### Spamassassin

To connect spamassassins spamd you need to set spamd_socket. The default is set
to localhost port 783.

spamd_socket (default: inet:127.0.0.1:783):


### String

  * string_strlen

  * string_strcmp

  * string_mailaddr
  

### Regex

  * regex_match

  * regex_imatch
  
### List

  * list_contains


