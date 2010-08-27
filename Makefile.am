AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
if BUILD_WITH_DEBUG
AM_CFLAGS += -Wextra -g -O0
endif
endif

include_HEADERS = credis.h
lib_LTLIBRARIES = libcredis.la

libcredis_la_LDFLAGS = -version-info @LIBCREDIS_CURRENT@:@LIBCREDIS_REVISION@:@LIBCREDIS_AGE@
# Only export functions which start with "credis_".
libcredis_la_LDFLAGS += -export-symbols-regex "^credis_"
libcredis_la_SOURCES = credis.c credis.h

noinst_PROGRAMS = credis-test

credis_test_SOURCES = credis-test.c
credis_test_LDADD = libcredis.la

