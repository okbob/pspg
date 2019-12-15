# ===========================================================================
#    https://www.gnu.org/software/autoconf-archive/ax_lib_postgresql.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_LIB_POSTGRESQL([MINIMUM-VERSION],[ACTION-IF-FOUND],[ACTION-IF-NOT-FOUND])
#
# DESCRIPTION
#
#   This macro provides tests of availability of PostgreSQL 'libpq' library
#   of particular version or newer.
#
#   AX_LIB_POSTGRESQL macro takes only one argument which is optional. If
#   there is no required version passed, then macro does not run version
#   test.
#
#   The --with-postgresql option takes one of three possible values:
#
#   no - do not check for PostgreSQL client library
#
#   yes - do check for PostgreSQL library in standard locations (pg_config
#   should be in the PATH)
#
#   path - complete path to pg_config utility, use this option if pg_config
#   can't be found in the PATH (You could set also PG_CONFIG variable)
#
#   This macro calls:
#
#     AC_SUBST(POSTGRESQL_CPPFLAGS)
#     AC_SUBST(POSTGRESQL_LDFLAGS)
#     AC_SUBST(POSTGRESQL_LIBS)
#     AC_SUBST(POSTGRESQL_VERSION)
#
#   And sets:
#
#     HAVE_POSTGRESQL
#
#   It execute if found ACTION-IF-FOUND (empty by default) and
#   ACTION-IF-NOT-FOUND (AC_MSG_FAILURE by default) if not found.
#
# LICENSE
#
#   Copyright (c) 2008 Mateusz Loskot <mateusz@loskot.net>
#   Copyright (c) 2014 Sree Harsha Totakura <sreeharsha@totakura.in>
#   Copyright (c) 2018 Bastien Roucaries <rouca@debian.org>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 22

AC_DEFUN([_AX_LIB_POSTGRESQL_OLD],[
	found_postgresql="no"
	_AX_LIB_POSTGRESQL_OLD_fail="no"
	while true; do
	  AC_CACHE_CHECK([for the pg_config program], [ac_cv_path_PG_CONFIG],
	    [AC_PATH_PROGS_FEATURE_CHECK([PG_CONFIG], [pg_config],
	      [[ac_cv_path_PG_CONFIG="";$ac_path_PG_CONFIG --includedir > /dev/null \
		&& ac_cv_path_PG_CONFIG=$ac_path_PG_CONFIG ac_path_PG_CONFIG_found=:]],
	      [ac_cv_path_PG_CONFIG=""])])
	  PG_CONFIG=$ac_cv_path_PG_CONFIG
	  AS_IF([test "X$PG_CONFIG" = "X"],[break])

	  AC_CACHE_CHECK([for the PostgreSQL libraries CPPFLAGS],[ac_cv_POSTGRESQL_CPPFLAGS],
		       [ac_cv_POSTGRESQL_CPPFLAGS="-I`$PG_CONFIG --includedir`" || _AX_LIB_POSTGRESQL_OLD_fail=yes])
	  AS_IF([test "X$_AX_LIB_POSTGRESQL_OLD_fail" = "Xyes"],[break])
	  POSTGRESQL_CPPFLAGS="$ac_cv_POSTGRESQL_CPPFLAGS"

	  AC_CACHE_CHECK([for the PostgreSQL libraries LDFLAGS],[ac_cv_POSTGRESQL_LDFLAGS],
		       [ac_cv_POSTGRESQL_LDFLAGS="-L`$PG_CONFIG --libdir`" || _AX_LIB_POSTGRESQL_OLD_fail=yes])
	  AS_IF([test "X$_AX_LIB_POSTGRESQL_OLD_fail" = "Xyes"],[break])
	  POSTGRESQL_LDFLAGS="$ac_cv_POSTGRESQL_LDFLAGS"

	  AC_CACHE_CHECK([for the PostgreSQL libraries LIBS],[ac_cv_POSTGRESQL_LIBS],
		       [ac_cv_POSTGRESQL_LIBS="-lpq"])
	  POSTGRESQL_LIBS="$ac_cv_POSTGRESQL_LIBS"

	  AC_CACHE_CHECK([for the PostgreSQL version],[ac_cv_POSTGRESQL_VERSION],
		       [
			ac_cv_POSTGRESQL_VERSION=`$PG_CONFIG --version | sed "s/^PostgreSQL[[[:space:]]][[[:space:]]]*\([[0-9.]][[0-9.]]*\).*/\1/"` \
			      || _AX_LIB_POSTGRESQL_OLD_fail=yes
		       ])
	  AS_IF([test "X$_AX_LIB_POSTGRESQL_OLD_fail" = "Xyes"],[break])
	  POSTGRESQL_VERSION="$ac_cv_POSTGRESQL_VERSION"


	  dnl
	  dnl Check if required version of PostgreSQL is available
	  dnl
	  AS_IF([test X"$postgresql_version_req" != "X"],[
	     AC_MSG_CHECKING([if PostgreSQL version $POSTGRESQL_VERSION is >= $postgresql_version_req])
	     AX_COMPARE_VERSION([$POSTGRESQL_VERSION],[ge],[$postgresql_version_req],
				[found_postgresql_req_version=yes],[found_postgresql_req_version=no])
	     AC_MSG_RESULT([$found_postgresql_req_version])
	  ])
	  AS_IF([test "Xfound_postgresql_req_version" = "Xno"],[break])

	  found_postgresql="yes"
	  break
	done
])

AC_DEFUN([_AX_LIB_POSTGRESQL_PKG_CONFIG],
[
  AC_REQUIRE([PKG_PROG_PKG_CONFIG])
  found_postgresql=no

  while true; do
    PKG_PROG_PKG_CONFIG
    AS_IF([test X$PKG_CONFIG = X],[break])

    _AX_LIB_POSTGRESQL_PKG_CONFIG_fail=no;
    AS_IF([test "X$postgresql_version_req" = "X"],
	  [PKG_CHECK_EXISTS([libpq],[found_postgresql_pkg_config=yes],[found_postgresql=no])],
	  [PKG_CHECK_EXISTS([libpq >= "$postgresql_version_req"],
			   [found_postgresql=yes],[found_postgresql=no])])
    AS_IF([test "X$found_postgresql" = "no"],[break])

    AC_CACHE_CHECK([for the PostgreSQL libraries CPPFLAGS],[ac_cv_POSTGRESQL_CPPFLAGS],
		   [ac_cv_POSTGRESQL_CPPFLAGS="`$PKG_CONFIG libpq --cflags-only-I`" || _AX_LIB_POSTGRESQL_PKG_CONFIG_fail=yes])
    AS_IF([test "X$_AX_LIB_POSTGRESQL_PKG_CONFIG_fail" = "Xyes"],[break])
    POSTGRESQL_CPPFLAGS="$ac_cv_POSTGRESQL_CPPFLAGS"


    AC_CACHE_CHECK([for the PostgreSQL libraries LDFLAGS],[ac_cv_POSTGRESQL_LDFLAGS],
		   [ac_cv_POSTGRESQL_LDFLAGS="`$PKG_CONFIG libpq --libs-only-L --libs-only-other`" || _AX_LIB_POSTGRESQL_PKG_CONFIG_fail=yes])
    AS_IF([test "X$_AX_LIB_POSTGRESQL_PKG_CONFIG_fail" = "Xyes"],[break])
    POSTGRESQL_LDFLAGS="$ac_cv_POSTGRESQL_LDFLAGS"


    AC_CACHE_CHECK([for the PostgreSQL libraries LIBS],[ac_cv_POSTGRESQL_LIBS],
		   [ac_cv_POSTGRESQL_LIBS="`$PKG_CONFIG libpq --libs-only-l`" || _AX_LIB_POSTGRESQL_PKG_CONFIG_fail=ye])
    AS_IF([test "X$_AX_LIB_POSTGRESQL_PKG_CONFIG_fail" = "Xyes"],[break])
    POSTGRESQL_LIBS="$ac_cv_POSTGRESQL_LIBS"

    dnl already checked by exist but need to be recovered
    AC_CACHE_CHECK([for the PostgreSQL version],[ac_cv_POSTGRESQL_VERSION],
		   [ac_cv_POSTGRESQL_VERSION="`$PKG_CONFIG libpq --modversion`" || _AX_LIB_POSTGRESQL_PKG_CONFIG_fail=yes])
    AS_IF([test "X$_AX_LIB_POSTGRESQL_PKG_CONFIG_fail" = "Xyes"],[break])
    POSTGRESQL_VERSION="$ac_cv_POSTGRESQL_VERSION"

    found_postgresql=yes
    break;
  done

])



AC_DEFUN([AX_LIB_POSTGRESQL],
[
    AC_ARG_WITH([postgresql],
	AS_HELP_STRING([--with-postgresql=@<:@ARG@:>@],
	    [use PostgreSQL library @<:@default=yes@:>@, optionally specify path to pg_config]
	),
	[
	AS_CASE([$withval],
		[[[nN]][[oO]]],[want_postgresql="no"],
		[[[yY]][[eE]][[sS]]],[want_postgresql="yes"],
		[
			want_postgresql="yes"
			PG_CONFIG="$withval"
		])
	],
	[want_postgresql="yes"]
    )

    AC_ARG_VAR([POSTGRESQL_CPPFLAGS],[cpp flags for PostgreSQL overriding detected flags])
    AC_ARG_VAR([POSTGRESQL_LIBFLAGS],[libs for PostgreSQL overriding detected flags])
    AC_ARG_VAR([POSTGRESQL_LDFLAGS],[linker flags for PostgreSQL overriding detected flags])

    # populate cache
    AS_IF([test "X$POSTGRESQL_CPPFLAGS" != X],[ac_cv_POSTGRESQL_CPPFLAGS="$POSTGRESQL_CPPFLAGS"])
    AS_IF([test "X$POSTGRESQL_LDFLAGS" != X],[ac_cv_POSTGRESQL_LDFLAGS="$POSTGRESQL_LDFLAGS"])
    AS_IF([test "X$POSTGRESQL_LIBS" != X],[ac_cv_POSTGRESQL_LIBS="$POSTGRESQL_LIBS"])

    postgresql_version_req=ifelse([$1], [], [], [$1])
    found_postgresql="no"

    POSTGRESQL_VERSION=""

    dnl
    dnl Check PostgreSQL libraries (libpq)
    dnl
    AS_IF([test X"$want_postgresql" = "Xyes"],[
      _AX_LIB_POSTGRESQL_PKG_CONFIG


      AS_IF([test X"$found_postgresql" = "Xno"],
	    [_AX_LIB_POSTGRESQL_OLD])

      AS_IF([test X"$found_postgresql" = Xyes],[
	  _AX_LIB_POSTGRESQL_OLD_CPPFLAGS="$CPPFLAGS"
	  CPPFLAGS="$CPPFLAGS $POSTGRESQL_CPPFLAGS"
	  _AX_LIB_POSTGRESQL_OLD_LDFLAGS="$LDFLAGS"
	  LDFLAGS="$LDFLAGS $POSTGRESQL_LDFLAGS"
	  _AX_LIB_POSTGRESQL_OLD_LIBS="$LIBS"
	  LIBS="$LIBS $POSTGRESQL_LIBS"
	  while true; do
	    dnl try to compile
	    AC_CHECK_HEADER([libpq-fe.h],[],[found_postgresql=no])
	    AS_IF([test "X$found_postgresql" = "Xno"],[break])
	    dnl try now to link
	    AC_CACHE_CHECK([for the PostgreSQL library linking is working],[ac_cv_postgresql_found],
	    [
	      AC_LINK_IFELSE([
		AC_LANG_PROGRAM(
		  [
		   #include <libpq-fe.h>
		  ],
		  [[
		    char conninfo[]="dbname = postgres";
		    PGconn     *conn;
		    conn = PQconnectdb(conninfo);
		  ]]
		 )
		],[ac_cv_postgresql_found=yes],
		  [ac_cv_postgresql_found=no])
	     ])
	    found_postgresql="$ac_cv_postgresql_found"
	    AS_IF([test "X$found_postgresql" = "Xno"],[break])
	    break
	done
	CPPFLAGS="$_AX_LIB_POSTGRESQL_OLD_CPPFLAGS"
	LDFLAGS="$_AX_LIB_POSTGRESQL_OLD_LDFLAGS"
	LIBS="$_AX_LIB_POSTGRESQL_OLD_LIBS"
	])


      AS_IF([test "x$found_postgresql" = "xyes"],[
		AC_DEFINE([HAVE_POSTGRESQL], [1],
			  [Define to 1 if PostgreSQL libraries are available])])
    ])

    AC_SUBST([POSTGRESQL_VERSION])
    AC_SUBST([POSTGRESQL_CPPFLAGS])
    AC_SUBST([POSTGRESQL_LDFLAGS])
    AC_SUBST([POSTGRESQL_LIBS])

    AS_IF([test "x$found_postgresql" = "xyes"],
     [ifelse([$2], , :, [$2])],
     [ifelse([$3], , AS_IF([test X"$want_postgresql" = "Xyes"],[AC_MSG_ERROR([Library requirements (PostgreSQL) not met.])],[:]), [$3])])

])
