bin_PROGRAMS = network-log
network_log_SOURCES = \
    network-log.c

barleywine_CFLAGS = -I$(top_srcdir)/include @LIBJSON_CFLAGS@ @HTTPD_CFLAGS@
barleywine_LDADD = -lc -lgcc -lpthread @LIBJSON_LIBS@ @HTTPD_LIBS@
