AC_INIT([pspg],[0],[pavel.stehule@gmail.com],[pspg])

AC_LANG([C])
AC_CONFIG_SRCDIR(src)

dnl  Document where we keep our .m4 file.
AC_CONFIG_MACRO_DIR([tools])
dnl  Make sure aclocal actually found it!
m4_pattern_forbid([^AX_])

AC_PROG_CC
AC_HEADER_STDC
AC_HEADER_TIOCGWINSZ
AX_DEBUG_CFLAGS
AX_WITH_CURSES
AX_WITH_CURSES_PANEL
AX_LIB_READLINE
AX_LIB_POSTGRESQL([],[], AC_MSG_NOTICE([PostgreSQL library not found]))

case "$ax_cv_ncurses" in "no")
	AC_MSG_ERROR([ncurses not found])
esac
case "$ax_cv_ncursesw" in "no")
	AC_MSG_WARN([The found ncurses library does not support wide-char.])
	AC_MSG_WARN([This means that pspg will not correctly render UTF-8.])
esac

AC_CHECK_LIB([m],[roundl])

AC_SEARCH_LIBS([clock_gettime], [rt posix4],
   [],
   [AC_MSG_ERROR([Function clock_gettime not available.])]
)

AC_SUBST(enable_debug)
AC_SUBST(CURSES_LIBS)
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_LDFLAGS)
AC_SUBST(COMPILE_MENU)
AC_SUBST(POSTGRESQL_CFLAGS)
AC_SUBST(POSTGRESQL_LDFLAGS)

AC_SUBST([POSTGRESQL_VERSION])
AC_SUBST([POSTGRESQL_CPPFLAGS])
AC_SUBST([POSTGRESQL_LDFLAGS])
AC_SUBST([POSTGRESQL_LIBS])



AC_CONFIG_FILES([config.make])

if test "$ax_cv_panel" = yes; then
  COMPILE_MENU=1
  CFLAGS="$CFLAGS -DCOMPILE_MENU"
fi

# supply -g if --enable-debug
if test "$enable_debug" = yes && test "$ac_cv_prog_cc_g" = yes; then
  CFLAGS="$CFLAGS -g"
fi

AC_CHECK_HEADERS([sys/inotify.h], have_inotify=yes, have_inotify=no)

AC_ARG_WITH([inotify],
            [AS_HELP_STRING([--without-inotify],
            [(Linux only) do not build with inotify support (default is yes)])],
            [with_inotify=no],
            [with_inotify=yes])

if test "x$have_inotify" = "xyes"; then
  if test "x$with_inotify" != "xyes"; then
    have_inotify=no
  else
    AC_DEFINE(HAVE_INOTIFY, 1, [Define if you have inotify support (linux only)])
  fi
fi

AC_OUTPUT
