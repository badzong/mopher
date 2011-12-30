How-to compile mopher on debian linux
=====================================


1. Prerequisites
----------------

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


# apt-get install gcc make flex bison libmilter-dev libdb4.8-dev libmysqlclient-dev


2. Fix missing Libmilter symlink
--------------------------------

I have no clue why the libmilter.so symlink is missing in the debian package.
Maybe I'll ask sometime. For now let's create the link manually.


  sh> cd /usr/lib
  sh> ln libmilter.so.1.0.1 libmilter.so


3. Compile mopher
-----------------

You probably already downloaded mopher. If not:

  sh> git clone https://github.com/badzong/mopher.git
  sh> cd mopher


Regular build procedure:

  sh> ./configure
  sh> make
  sh> make install
