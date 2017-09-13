# gcc default/debug CFLAGS handling respecting user's CFLAGS
# avoid AC_PROG_CC setting -O2 CFLAGS which will override DEBUG_CFLAGS' -O0
# must be used right after AC_INIT

AC_DEFUN([AX_DEBUG_CFLAGS], [

# ensure CFLAGS are set
AS_IF([test "${CFLAGS+set}"], [
	USE_DEFAULT_CFLAGS=false
], [
	USE_DEFAULT_CFLAGS=true
	CFLAGS=""
])
AC_PROG_CC

# add --enable-debug arg
AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [enable debug build]), [], [])
AS_IF([test "$enable_debug" = "yes"], [
	# add the DEBUG pre-processor define
	AC_DEFINE([DEBUG], [1], [debug build])
	# gcc/gdb debug options
	AS_IF([test "$GCC" = "yes"], [
		DEBUG_CFLAGS="-ggdb -O0"
	], [
		ACX_DEBUG_CFLAGS_G
	])
], [
	# what AC_PROG_CC would have done if CFLAGS were not set
	AS_IF([$USE_DEFAULT_CFLAGS], [
		ACX_DEBUG_CFLAGS_G 
		AS_IF([test "$GCC" = "yes"], [
			DEBUG_CFLAGS="$DEBUG_CFLAGS -O2"
		])
	])
])
AC_SUBST([DEBUG_CFLAGS], [$DEBUG_CFLAGS])

])

AC_DEFUN([ACX_DEBUG_CFLAGS_G], [
	# default to -g
	AS_IF([test "$ac_cv_prog_cc_g" = "yes"], [
		DEBUG_CFLAGS="-g"
	], [
		DEBUG_CFLAGS=""
	])
])

