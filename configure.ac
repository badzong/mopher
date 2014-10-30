AC_PREREQ([2.64])
AC_INIT(m4_esyscmd([printf %s ${DIST:-unnamed}]),
  m4_esyscmd([printf %s ${VERSION:-0.0.0}]),
  m4_esyscmd([printf %s ${CONTACT:-nonexistent}]))

AC_SUBST([PACKAGE], [$PACKAGE_NAME])
AC_SUBST([VERSION], [$PACKAGE_VERSION])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_YACC
AC_PROG_CC
AC_PROG_LEX
AC_PROG_MAKE_SET


AC_PATH_PROG([OBJCOPY], [objcopy])
if test -z "$OBJCOPY"
then
  AC_MSG_ERROR([objcopy not found. Make sure you have GNU binutils installed.])
fi

# check for install
AC_PATH_PROG([INSTALL], [install])
if test -z "$INSTALL"
then
  AC_MSG_WARN([install not found.])
fi

# check for sed
AC_PATH_PROG([SED], [sed])
if test -z "$SED"
then
  AC_MSG_WARN([sed not found.])
fi

# check for rmdir
AC_PATH_PROG([RMDIR], [rmdir])
if test -z "$RMDIR"
then
  AC_MSG_WARN([rmdir not found.])
fi

# check for rm
AC_PATH_PROG([RM], [rm])
if test -z "$RM"
then
  AC_MSG_WARN([rm not found.])
fi

# check for ld
AC_PATH_PROG([LD], [ld])
if test -z "$LD"
then
  AC_MSG_WARN([ld not found.])
fi

# check for test
AC_PATH_PROG([TEST], [test])
if test -z "$TEST"
then
  AC_MSG_WARN([test not found.])
fi

# Checks prerequisites

AC_CHECK_LIB([pthread], [pthread_create],
	[LIBS="$LIBS -lpthread"],
	[AC_MSG_ERROR([libpthread not found. Make sure your system has libpthread])]
)

AC_CHECK_LIB([milter], [smfi_setconn],
	[LIBS="$LIBS -lmilter"],
	[AC_MSG_ERROR([libmilter not found. Make sure your system has libmilter])]
)

AC_CHECK_LIB([idn], [idna_to_ascii_8z],
	[LIBS="$LIBS -lidn"],
	[AC_MSG_ERROR([libidn not found. Make sure your system has libidn])]
)

AC_CHECK_FUNCS([dlopen dlsym dlerror dlclose],
  [],
  AC_CHECK_LIB([dl], [dlopen],
    [ LIBS="$LIBS -ldl" ],
    AC_MSG_ERROR([libc has no dlopen and libdl was not found.])
  )
)

# Check for BerkeleyDB support
AC_ARG_WITH([bdb], AS_HELP_STRING([--with-bdb], [build with BerkeleyDB support]))

have_bdb=no
BDB_DB185_H=
BDB_LIBS=

# db 1.85, included in libc
AC_CHECK_FUNCS(dbopen, [AC_CHECK_HEADERS([db.h], [have_bdb=yes])])

# db 1.85 compatibility mode
if test "$have_bdb" = "no"
then
  AC_CHECK_LIB(db, __db185_open, [AC_CHECK_HEADERS(db_185.h,
    [have_bdb=yes; BDB_LIBS='-ldb'; BDB_DB185_H='-DBDB_DB185_H'])])
fi

AS_IF(
  [test -z "$with_bdb"], [with_bdb="$have_bdb"],
  [test "$with_bdb" = "yes" -a "$have_bdb" = "no"],
    AC_MSG_FAILURE([libdb failed (--without-bdb to disable)])
)

if test "$with_bdb" = "yes"
then
  MOD_DB_BDB="bdb.so"
  AC_SUBST([MOD_DB_BDB])
  AC_SUBST([BDB_LIBS])
  AC_SUBST([BDB_DB185_H])
  AC_DEFINE([WITH_MOD_BDB])
fi

# Check for MySQL support
AC_ARG_WITH([mysql], AS_HELP_STRING([--with-mysql], [build with MySQL support]))
AC_CHECK_LIB([mysqlclient], [mysql_real_connect], [have_mysql="yes"], [have_mysql="no"])

AS_IF(
  [test -z "$with_mysql"], [with_mysql="$have_mysql"],
  [test "$with_mysql" = "yes" -a "$have_mysql" = "no"],
    AC_MSG_FAILURE([libmysqlclient failed (--without-mysql to disable)])
)

if test "$with_mysql" = "yes"
then
  MOD_DB_MYSQL="sakila.so"
  AC_SUBST([MOD_DB_MYSQL])
  AC_DEFINE([WITH_MOD_MYSQL])
  MOD_LIBS="$MOD_LIBS -lmysqlclient"
fi


# Check for spf2 support
AC_ARG_WITH([spf2], AS_HELP_STRING([--with-spf2], [build with SPF support (libspf2)]))
AC_CHECK_LIB([spf2], [SPF_request_new], [have_spf2="yes"], [have_spf2="no"])

AS_IF(
  [test -z "$with_spf2"], [with_spf2="$have_spf2"],
  [test "$with_spf2" = "yes" -a "$have_spf2" = "no"],
    AC_MSG_FAILURE([libspf2 failed (--without-spf2 to disable)])
)

if test "$with_spf2" = "yes"
then
  MOD_ACL_SPF="spf.so"
  AC_SUBST([MOD_ACL_SPF])
  AC_DEFINE([WITH_MOD_SPF])
  AS_IF([test "$enable_dynamic" = "no"], [MOD_LIBS="$MOD_LIBS -lspf2"])
fi


AC_SUBST([MOD_LIBS])

# So far no further checks

AC_CONFIG_FILES([
	config/Makefile
	make/mk.common
	man/Makefile
	src/Makefile
	src/modules/Makefile
])

AC_OUTPUT
