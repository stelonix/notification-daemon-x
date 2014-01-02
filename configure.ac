dnl Process this file with autoconf to create configure.

dnl ################################################################
dnl # Initialize autoconf
dnl ################################################################
AC_INIT([notification-daemon],[0.7.6],[https://bugzilla.gnome.org/enter_bug.cgi?product=notification-daemon],[notification-daemon])
AC_PREREQ(2.63)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([macros])

dnl ################################################################
dnl # Version information
dnl ################################################################
NOTIFICATION_DAEMON_MAJOR_VERSION=0
NOTIFICATION_DAEMON_MINOR_VERSION=7
NOTIFICATION_DAEMON_MICRO_VERSION=6
NOTIFICATION_DAEMON_DEVEL_VERSION=0

NOTIFICATION_DAEMON_VERSION=$NOTIFICATION_DAEMON_MAJOR_VERSION.$NOTIFICATION_DAEMON_MINOR_VERSION.$NOTIFICATION_DAEMON_MICRO_VERSION

if test "x$NOTIFICATION_DAEMON_DEVEL_VERSION" != "x0"; then
	if test "x$NOTIFICATION_DAEMON_MICRO_VERSION" = "x0"; then
		NOTIFICATION_DAEMON_MICRO_VERSION=$NOTIFICATION_DAEMON_DEVEL_VERSION
		NOTIFICATION_DAEMON_VERSION=$NOTIFICATION_DAEMON_MAJOR_VERSION.$NOTIFICATION_DAEMON_MINOR_VERSION.$NOTIFICATION_DAEMON_DEVEL_VERSION
	else
		NOTIFICATION_DAEMON_VERSION=$NOTIFICATION_DAEMON_VERSION.$NOTIFICATION_DAEMON_DEVEL_VERSION
	fi
fi

AC_DEFINE_UNQUOTED(NOTIFICATION_DAEMON_MAJOR_VERSION, $NOTIFICATION_DAEMON_MAJOR_VERSION,
	[notification-daemon major version.])
AC_DEFINE_UNQUOTED(NOTIFICATION_DAEMON_MINOR_VERSION, $NOTIFICATION_DAEMON_MINOR_VERSION,
	[notification-daemon minor version.])
AC_DEFINE_UNQUOTED(NOTIFICATION_DAEMON_MICRO_VERSION, $NOTIFICATION_DAEMON_MICRO_VERSION,
	[notification-daemon micro version.])
AC_DEFINE_UNQUOTED(NOTIFICATION_DAEMON_VERSION, "$NOTIFICATION_DAEMON_VERSION",
	[notification-daemon version.])

dnl ################################################################
dnl # Initialize automake
dnl ################################################################
AM_INIT_AUTOMAKE([1.10 no-dist-gzip dist-bzip2])
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ################################################################
dnl # Check for some standard stuff.
dnl ################################################################
# Checks for programs
AC_PROG_CC
AC_PROG_CPP
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_PROG_INSTALL

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

dnl ---------------------------------------------------------------------------
dnl Initialize Libtool
dnl ---------------------------------------------------------------------------
LT_PREREQ([2.2.6])
LT_INIT


dnl ---------------------------------------------------------------------------
dnl i18n stuff
dnl ---------------------------------------------------------------------------
IT_PROG_INTLTOOL([0.40.0])

AC_SUBST([GETTEXT_PACKAGE], [notification-daemon])
AM_GNU_GETTEXT_VERSION([0.11])
AM_GLIB_GNU_GETTEXT
AC_DEFINE([GETTEXT_PACKAGE], [PACKAGE_TARNAME], [Define to the gettext package name.])


dnl ---------------------------------------------------------------------------
dnl Requirements for the daemon
dnl ---------------------------------------------------------------------------
REQ_GTK_VERSION=2.91.0
REQ_GLIB_VERSION=2.27.0
REQ_LIBCANBERRA_GTK_VERSION=0.4
pkg_modules="
	gtk+-3.0 >= $REQ_GTK_VERSION, \
	glib-2.0 >= $REQ_GLIB_VERSION, \
        gio-2.0 >= $REQ_GLIB_VERSION, \
        libcanberra-gtk3 >= $REQ_LIBCANBERRA_GTK_VERSION, \
        x11 \
"
PKG_CHECK_MODULES(NOTIFICATION_DAEMON, $pkg_modules)
AC_SUBST(NOTIFICATION_DAEMON_CFLAGS)
AC_SUBST(NOTIFICATION_DAEMON_LIBS)

AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
AS_AC_EXPAND(LIBDIR, $libdir)
AS_AC_EXPAND(DATADIR, $datadir)
AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

AC_ARG_WITH(dbus-sys, [  --with-dbus-sys=<dir>   where D-BUS system.d directory is])
AC_ARG_WITH(dbus-services, [  --with-dbus-services=<dir>   where D-BUS services directory is])

if ! test -z "$with_dbus_sys" ; then
	DBUS_SYS_DIR="$with_dbus_sys"
else
	DBUS_SYS_DIR="$SYSCONFDIR/dbus-1/system.d"
fi

if ! test -z "$with_dbus_services" ; then
	DBUS_SERVICES_DIR="$with_dbus_services"
else
	DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
fi

AC_SUBST(DBUS_SYS_DIR)
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SYSTEMD_DIR, "$DBUS_SYS_DIR", [Where system.d dir for DBUS is])
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [Where services dir for DBUS is])

dnl ---------------------------------------------------------------------------
dnl Turn on the additional warnings last
dnl ---------------------------------------------------------------------------
GNOME_COMPILE_WARNINGS([maximum])
GNOME_CXX_WARNINGS([yes])
GNOME_MAINTAINER_MODE_DEFINES

AC_ARG_ENABLE(more-warnings,
              AS_HELP_STRING([--enable-more-warnings],
                             [Maximum compiler warnings]),
              set_more_warnings="$enableval",[
	      if test -d $srcdir/.git; then
	        set_more_warnings=yes
	      else
	        set_more_warnings=no
              fi])

AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        WARNINGCFLAGS="\
        -Wchar-subscripts -Wmissing-declarations \
        -Wcast-align -Wsign-compare -Wp,-D_FORTIFY_SOURCE=2 \
        $WARNINGCFLAGS"

        for option in -Wno-strict-aliasing; do
                SAVE_CFLAGS="$WARNINGCFLAGS"
                WARNINGCFLAGS="$WARNINGCFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
                if test $has_option = no; then
                        WARNINGCFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi

AM_CFLAGS="$AM_CFLAGS $WARN_CFLAGS $WARNINGCFLAGS"
AM_CXXFLAGS="$AM_CXXFLAGS $WARN_CXXFLAGS"

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])

AC_CONFIG_FILES([
Makefile
data/Makefile
data/notification-daemon.desktop.in
po/Makefile.in
src/Makefile
])

AC_OUTPUT

echo "
                    $PACKAGE_NAME $PACKAGE_VERSION
                    =========================

	prefix:                   ${prefix}
	exec_prefix:              ${exec_prefix}
        bindir:                   ${bindir}
        datadir:                  ${datadir}
	source code location:	  ${srcdir}
	cflags:		          ${AM_CFLAGS}

        dbus-1 system.d           $DBUS_SYS_DIR
        dbus-1 services           $DBUS_SERVICES_DIR

"
