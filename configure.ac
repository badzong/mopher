AC_PREREQ([2.64])
AC_INIT([mopher], [0.0], [manuel@badzong.com])

AC_SUBST([PACKAGE], [$PACKAGE_NAME])
AC_SUBST([VERSION], [$PACKAGE_VERSION])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_YACC
AC_PROG_CC
AC_PROG_LEX
AC_PROG_MAKE_SET

AC_PATH_PROG([OBJCOPY], [objcopy], [])
if test -z "$OBJCOPY"
then
  AC_MSG_ERROR([objcopy not found. Make sure you have GNU binutils installed.])
fi

# tar and bzip2 are used to make dist.
AC_PATH_PROG([BZIP2], [bzip2], [])
if test -z "$BZIP2"
then
  AC_MSG_WARN([bzip not found.])
fi

AC_PATH_PROG([TAR], [tar], [])
if test -z "$TAR"
then
  AC_MSG_WARN([tar not found.])
fi


# Checks for libraries.

# Check if we need to link against libdl
AC_CHECK_FUNCS(
  [dlopen dlsym dlerror dlclose],
  [have_dl="yes"],
  [have_dl="no"]
)

if test "$have_dl" = "no"
then
  AC_CHECK_LIB(
    [dl],
    [dlopen],
    [
      LIBS="$LIBS -ldl"
      have_dl="yes"
    ]
  )
fi

if test "$have_dl" = "no"
then
  AC_MSG_ERROR([dlopen not in libc and libdl not found.])
fi

AC_CHECK_LIB([pthread], [pthread_create],
	[LIBS="$LIBS -lpthread"],
	[AC_MSG_ERROR([libpthread not found. Make sure your system has libpthread])]
)

AC_CHECK_LIB([milter], [smfi_setconn],
	[LIBS="$LIBS -lmilter"],
	[AC_MSG_ERROR([libmilter not found. Make sure your system has libmilter])]
)


# Check for BerkeleyDB support
AC_CHECK_LIB(
  [db],
  [db_create],
  [have_bdb="yes"],
  [have_bdb="no"]
)

AC_ARG_WITH([bdb], AS_HELP_STRING([--with-bdb], [build with BerkeleyDB support]))

if test "$with_bdb" = "yes" -a "$have_bdb" = "no"
then
  AC_MSG_FAILURE([libdb failed (--without-bdb to disable)])
fi

if test "$have_bdb" = "yes"
then
  MOD_DB_BDB="bdb.so"
  AC_SUBST([MOD_DB_BDB])
fi


# Check for MySQL support
AC_CHECK_LIB(
  [mysqlclient],
  [mysql_real_connect],
  [have_mysql="yes"],
  [have_mysql="no"]
)

AC_ARG_WITH([mysql], AS_HELP_STRING([--with-mysql], [build with MySQL support]))

if test "$with_mysql" = "yes" -a "$have_mysql" = "no"
then
  AC_MSG_FAILURE([libmysqlclient failed (--without-mysql to disable)])
fi

if test "$have_mysql" = "yes"
then
  MOD_DB_MYSQL="my.so"
  AC_SUBST([MOD_DB_MYSQL])
fi

AC_CHECK_LIB(
  [spf2],
  [SPF_request_new],
  [have_spf2="yes"],
  [have_spf2="no"]
)

AC_ARG_WITH([spf2], AS_HELP_STRING([--with-spf2], [build with SPF support (libspf2)]))

if test "$with_spf2" = "yes" -a "$have_spf2" = "no"
then
  AC_MSG_FAILURE([libspf2 failed (--without-spf2 to disable)])
fi

if test "$have_spf2" = "yes"
then
  MOD_ACL_SPF="spf.so"
  AC_SUBST([MOD_ACL_SPF])
fi

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h malloc.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([acl bzero gethostname memset socket strchr strdup strerror strrchr strstr])

AC_CONFIG_FILES([Makefile src/Makefile src/mod/acl/Makefile src/mod/db/Makefile
  src/mod/tables/Makefile])

AC_OUTPUT
