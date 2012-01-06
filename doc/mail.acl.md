# mail.acl - mail access control list for mopher


## INTRODUCTION
The mopher daemon *mopherd* uses 2 configuration files *mopherd.conf* and
*mail.acl*. The former controls the behaviour of the daemon itself while the
latter controls how incoming mail is filtered. By default mopherd looks for
both configuration files in */etc/mopher* (or whatever *sysconfig* path you
used in the build process) Alternative locations may be specified using the
*mopherd* command line argument *-c* for *mopherd.conf* or the configuration
directive *acl_path* for mail.acl.


## DESCRIPTION
The file mail.acl contains information about how mopherd should filter incoming
email. The syntax of mail.acl statements was derived from common firewall
configuration. A statement is either a rule or a definition. Statements may
spawn multiple lines. Multiple whitespaces and emtpy lines are ignored. #
starts a comment until the end of the line.


### Basic syntax
```
**stage** *expression* action
```

## MILTER STAGES
mopher uses libmilter and inhertits its basic architecture. To filter email
with mopher you should be at least familiar with the different milter stages.
Best have a look at the
[libmilter documentation](https://www.milter.org/developers.)

### Stages

*libmilter* defines the following stages:

* connect
* helo
* envfrom
* envrcpt
* data
* header
* eoh
* body
* eom
* close
* abort
* unknwon

Mopher has a rule table for every stage. If for example you want to log *Hello
World* every time someone connects to your mail server, you could add the
following rule to your *mail.acl*:

```
connect log "Hello World"
```

*connect* is the table the rule is added to and *log* is the action that
is executed.


## EXPRESSIONS

For every milter stage mopher checks all rules in the corespondig table in
order of definition (top to bottom). If an expression evaluates boolean true
the specified action is executed.

### Basic examples
```
connect 1 == 1 log "logged for every new connection"
connect 4 / 4 == 1 "logged"
connect 1 == 2 log "never logged"

eom milter_message_size > 1M log "big message: " + milter_message_size + " bytes"
```

### More useful
```
connect rbl_spamhaus = 127.0.0.2 tarpit 60s

# CAVEAT: You need to configure rbl_spamhaus in your *mopherd.conf*
# rbl = {
#   "rbl_spamhaus" = "zen.spamhaus.org",
#   "rbl_sorbs" = "dnsbl.sorbs.net"
# }
```

## ACTIONS

Currently mopher supports the following actions:

* continue
* reject
* discard
* accept
* tempfail
* greylist
* tarpit
* log
* set
* jump


### Continue

Stop evaluation at this stage and continue to the next stage. This skips all
following rules in the current stage.


### Reject

Reject this message with a SMTP 5xx code. Optionally you can specify response
code and message.

#### Examples
```
envfrom milter_envfrom = "nemesis@rival.xyz" reject
eom spamd_score > 15 reject xcode 550 message "Not accepting spam"
```

### Discard

Discard the message silently. The email server accepts the email but does not
queue it.

### Accept

Accept the message and skip all following rules and stages.

### Tempfail

Reject this message with a SMTP 4xx code. This means the error is temporary and
the remote email server will keep this message in the queue and retries
delivery later or possibly relaying through another MX.

### Greylist

Greylist this message until requirements are met. You can specify a greylist
timeout, a retry count, deadline and visa period.

```
envrcpt greylist delay 15m attempts 2 deadline 2h visa 7d
# This means greylist for 15 minutes and 2 delivery attempts within the next 2
# hours. If passed keep the greylist record as a visa for 7 days.
```

### Tarpit

Tarpit the connection for the given time.

```
connect tarpit 10s
```

### Log

Log a message to your systems mail.log.

```
connect log "New connection from " + milter_hostname
```

### Set

Set a variable.

```
connect rbl_spamhaus == 127.0.0.2 set $badhost = 1
connect rbl_sorbs == 127.0.0.3 set $badhost = 1
connect $badhost == 1 tarpit 1m
```

### Jump

Jump to another stage. If you like you can jump to another stage or you can
define your own stage.

```
envrcpt rbl_spamhaus == 127.0.0.2 jump spam
envrcpt rbl_sorbs == 127.0.0.3 jump spam
envrcpt spf == SPF_FAIL jump spam

spam tarpit 30s
spam greylist 2h attempts 5 deadline 12h visa 7d
```

## DEFINITIONS

Definitions can be used as shorthands for long or frequently used expressions.
A definition starts with the keyword *define* followed by an *identifier*
followed by an *expression*.

```
define rbl_match rbl_spamhaus == 127.0.0.2 || rbl_sorbs == 127.0.0.3

connect rbl_match tarpit 1m
```

## Example

This simple example should already filter quite a lot of spam.

```
connect log milter_id + ":: new connection: hostname=" + milter_hostname + " (" + milter_hostaddr + ")"
connect tarpit 10s

envrcpt log milter_id + ":: envelope: from=" + string_mailaddr(milter_envfrom) + " rcpt=" + string_mailaddr(milter_envrcpt)
envrcpt spf == SPF_FAIL greylist delay 1h attempts 4 deadline 12h visa 14d
envrcpt greylist delay 0 attempts 4 deadline 8h visa 14d

eom tarpit 20s
eom log milter_id + ":: message: queue_id=" + milter_queueid + " message_size=" + milter_message_size
eom tarpit_delayed add header "X-Tarpit" value "message tarpitted for " + tarpit_delayed + " seconds"
eom greylist_delayed add header "X-Greylist" value "message greylisted for " + greylist_delayed + " seconds"

close log milter_id + ":: connection: closed"
```

## AUTHORS
This manuel page was written by Manuel Badzong <mopher@badzong.com>.
