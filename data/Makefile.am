@INTLTOOL_DESKTOP_RULE@
desktopdir= $(datadir)/applications
desktop_in_in_files = notification-daemon.desktop.in.in
desktop_in_files = notification-daemon.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = \
	$(desktop_in_in_files)

CLEANFILES = \
	$(desktop_DATA)

DISTCLEANFILES = \
	$(desktop_in_files)

-include $(top_srcdir)/git.mk
