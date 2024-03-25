bin_PROGRAMS = network-log
network_log_SOURCES = \
    device_stat.c     \
    http.c            \
    hw_use.c          \
    network-log.c

network_log_CFLAGS = -I$(top_srcdir)/include @LIBJSON_CFLAGS@ @HTTPD_CFLAGS@
network_log_LDADD = -lc -lgcc -lpthread @LIBJSON_LIBS@ @HTTPD_LIBS@
