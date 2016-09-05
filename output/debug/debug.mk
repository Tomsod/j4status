plugins_LTLIBRARIES += \
	debug.la \
	$(null)

debug_la_SOURCES = \
	output/debug/src/debug.c \
	$(null)

debug_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-D G_LOG_DOMAIN=\"j4status-debug\" \
	$(null)

debug_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

debug_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-module -avoid-version -export-symbols-regex j4status_output \
	$(null)

debug_la_LIBADD = \
	libj4status-plugin.la \
	$(GIO_LIBS) \
	$(GLIB_LIBS) \
	$(null)
