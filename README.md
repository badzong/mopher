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

I wrote most of mopher between 2009 and 2010. After that it ran quite stable
over a year without any code changes. Because I never found the time to write
usable documentation I didn't release it publicly. Most of the documentation is
still missing. Feel free to contribute.


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
  * SPF (libspf2)
  * BerkelyDB
  * MySQL


Compiles on:

  * Linux
  * NetBSD
  * FreeBSD
  * Maybe more


Questions & Answers
-------------------

There's a mopher [google group](https://groups.google.com/group/mopher).


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


Contributors
------------

Petar Bogdanovic


License (MIT)
-------------

Copyright (c) 2011 Manuel Badzong

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
