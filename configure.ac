AC_INIT([network-log], [0.1], [otavio.car.borges@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_PROG_CC

PKG_CHECK_MODULES(LIBJSON, [json-c >= 0.15])
PKG_CHECK_MODULES(HTTPD, [libmicrohttpd >= 0.9])
AC_CONFIG_HEADERS([config.h])
AC_DEFINE([APP_YEAR], [2023], [The year the application was last updated])

AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_OUTPUT
