plugins_LTLIBRARIES += \
	upower.la \
	$(null)

man5_MANS += \
	input/upower/man/j4status-upower.conf.5 \
	$(null)


upower_la_SOURCES = \
	input/upower/src/upower.c \
	$(null)

upower_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-D G_LOG_DOMAIN=\"j4status-upower\" \
	$(null)

upower_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(UPOWER_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

upower_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-module -avoid-version -export-symbols-regex j4status_input \
	$(null)

upower_la_LIBADD = \
	libj4status-plugin.la \
	$(UPOWER_LIBS) \
	$(GLIB_LIBS) \
	$(null)
