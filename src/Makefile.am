bin_PROGRAMS = fagbnc

fagbnc_SOURCES = fagbnc.c ucbase.cpp ucbase.h qwrap.cpp qwrap.h
fagbnc_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/common
fagbnc_LDADD = ../common/libfagcommon.la -lsrsbsns -lsrsirc -lsrslog
