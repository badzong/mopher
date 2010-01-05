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


AC_PATH_PROG([OBJCOPY], [objcopy])
if test -z "$OBJCOPY"
then
  AC_MSG_ERROR([objcopy not found. Make sure you have GNU binutils installed.])
fi

AC_PATH_PROG([AR], [ar])
if test -z "$AR"
then
  AC_MSG_ERROR([ar not found. Make sure you have GNU binutils installed.])
fi

# tar and gzip are used to make dist.
AC_PATH_PROG([GZIP], [gzip])
if test -z "$GZIP"
then
  AC_MSG_WARN([gzip not found.])
fi

AC_PATH_PROG([TAR], [tar])
if test -z "$TAR"
then
  AC_MSG_WARN([tar not found.])
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

# Checks for libraries.

AC_CHECK_LIB([pthread], [pthread_create],
	[LIBS="$LIBS -lpthread"],
	[AC_MSG_ERROR([libpthread not found. Make sure your system has libpthread])]
)

AC_CHECK_LIB([milter], [smfi_setconn],
	[LIBS="$LIBS -lmilter"],
	[AC_MSG_ERROR([libmilter not found. Make sure your system has libmilter])]
)

# Build with module support (default = yes)
AC_ARG_ENABLE([dynamic], AS_HELP_STRING([--enable-dynamic], [compile modules as loadable shared objects]))
if test ! "$enable_dynamic" = "no"
then
  # Check if we need to link against libdl
  AC_CHECK_FUNCS([dlopen dlsym dlerror dlclose], [have_dl="yes"], [have_dl="no"])

  if test "$have_dl" = "no"
  then
    AC_CHECK_LIB([dl], [dlopen], [ LIBS="$LIBS -ldl" have_dl="yes"])
  fi

  if test "$have_dl" = "no"
  then
    AC_MSG_ERROR([libc has no dlopen and libdl was not found.])
  fi

  AC_DEFINE([DYNAMIC])
  MOD_ALL="\$(MODULES)"
  mod_suffix="so"
else
  MOD_ALL="lib.a \$(OBJECTS)"
  MOD_LIBS="modules/lib.a"
  mod_suffix="o"
fi


# Check for BerkeleyDB support
AC_ARG_WITH([bdb], AS_HELP_STRING([--with-bdb], [build with BerkeleyDB support]))
AC_CHECK_LIB([db], [db_create], [have_bdb="yes"], [have_bdb="no"])

AS_IF(
  [test -z "$with_bdb"], [with_bdb="$have_bdb"],
  [test "$with_bdb" = "yes" -a "$have_bdb" = "no"],
    AC_MSG_FAILURE([libdb failed (--without-bdb to disable)])
)

if test "$with_bdb" = "yes"
then
  MOD_DB_BDB="bdb.$mod_suffix"
  AC_SUBST([MOD_DB_BDB])
  AC_DEFINE([WITH_MOD_BDB])
  AS_IF([test "$enable_dynamic" = "no"], [MOD_LIBS="$MOD_LIBS -ldb"])
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
  MOD_DB_MYSQL="sakila.$mod_suffix"
  AC_SUBST([MOD_DB_MYSQL])
  AC_DEFINE([WITH_MOD_MYSQL])
  AS_IF([test "$enable_dynamic" = "no"], [MOD_LIBS="$MOD_LIBS -lmysqlclient"])
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
  MOD_ACL_SPF="spf.$mod_suffix"
  AC_SUBST([MOD_ACL_SPF])
  AC_DEFINE([WITH_MOD_SPF])
  AS_IF([test "$enable_dynamic" = "no"], [MOD_LIBS="$MOD_LIBS -lspf2"])
fi


AC_SUBST([MOD_LIBS])
AC_SUBST([MOD_ALL])


# So far no further checks


# Checks for header files.
# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.

AC_CONFIG_FILES([
	Makefile
	Makefile.common
	src/Makefile
	src/modules/Makefile
	man/Makefile
])

AC_OUTPUT
