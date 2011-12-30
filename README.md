MOPHER
======


Table of contents
-----------------

  * Preamble
  * Installation
  * Bug reports
  * License


Preamble
--------

I wrote mopher between 2009 and 2010 as a replacement for milter-greylist. It
ran quite stable over a year without any code changes. Because I never found
the time to write usable documentation I didn't release it publicly. Most of
the documentation is still missing. Feel free to contribute.


Design goals:

  * Lightweight and modular design
  * Firewall like configuration
  * Fully support libmilter features (early 2010)
  * Flexible greylisting (time, retry count)
  * Filter spam efficiently


Features:

  * Greylisting
  * Tarpitting
  * Spamassassin
  * RBL
  * BerkelyDB
  * MySQL


Compiles on:

  * Linux
  * NetBSD
  * FreeBSD
  * Maybe more


FAQ
---

What does mopher mean?

```
  milter + gopher = mopher
```

> Other cool mail filter names have already been taken. At least it's unique.


Why should I use it?

> To get rid of spam in a very flexible, firewally and lightweight way.


I opperate this huge mail infrastucture. Is mopher scalable?

> Highly. It has a built-in memory database similar to milter-greylists and
> supports BerkeleyDB and MySQL tables (I'll write a PostgreSQL driver soon).


Why should I join mopher development?

> Because it's really easy to create new custom modules for mopher. Don't let
> the missing documentation scare you! Maybe have a look at
> src/modules/string.c


Installation
------------

```
# ./configure
# make
# make install
```


Configuration
-------------

Configuration examples can be found in the config folder.
For the gory details on access control see src/acl_yacc.y


Bug reports
-----------

Please send bug reports to: mopher@badzong.com


License
-------

See LICENSE
