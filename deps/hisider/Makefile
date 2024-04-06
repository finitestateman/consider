# Hisider Makefile
# Copyright (C) 2010-2011 Salvatore Sanfilippo <antirez at gmail dot com>
# Copyright (C) 2010-2011 Pieter Noordhuis <pcnoordhuis at gmail dot com>
# This file is released under the BSD license, see the COPYING file

OBJ=alloc.o net.o hisider.o sds.o async.o read.o sockcompat.o
EXAMPLES=hisider-example hisider-example-libevent hisider-example-libev hisider-example-glib hisider-example-push hisider-example-poll
TESTS=hisider-test
LIBNAME=libhisider
PKGCONFNAME=hisider.pc

HIREDIS_MAJOR=$(shell grep HIREDIS_MAJOR hisider.h | awk '{print $$3}')
HIREDIS_MINOR=$(shell grep HIREDIS_MINOR hisider.h | awk '{print $$3}')
HIREDIS_PATCH=$(shell grep HIREDIS_PATCH hisider.h | awk '{print $$3}')
HIREDIS_SONAME=$(shell grep HIREDIS_SONAME hisider.h | awk '{print $$3}')

# Installation related variables and target
PREFIX?=/usr/local
INCLUDE_PATH?=include/hisider
LIBRARY_PATH?=lib
PKGCONF_PATH?=pkgconfig
INSTALL_INCLUDE_PATH= $(DESTDIR)$(PREFIX)/$(INCLUDE_PATH)
INSTALL_LIBRARY_PATH= $(DESTDIR)$(PREFIX)/$(LIBRARY_PATH)
INSTALL_PKGCONF_PATH= $(INSTALL_LIBRARY_PATH)/$(PKGCONF_PATH)

# sider-server configuration used for testing
REDIS_PORT=56379
REDIS_SERVER=sider-server
define REDIS_TEST_CONFIG
	daemonize yes
	pidfile /tmp/hisider-test-sider.pid
	port $(REDIS_PORT)
	bind 127.0.0.1
	unixsocket /tmp/hisider-test-sider.sock
endef
export REDIS_TEST_CONFIG

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $${CC%% *} >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $${CXX%% *} >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -Wextra -Werror -Wstrict-prototypes -Wwrite-strings -Wno-missing-field-initializers
DEBUG_FLAGS?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(DEBUG_FLAGS) $(PLATFORM_FLAGS)
REAL_LDFLAGS=$(LDFLAGS)

DYLIBSUFFIX=so
STLIBSUFFIX=a
DYLIB_MINOR_NAME=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_SONAME)
DYLIB_MAJOR_NAME=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR)
DYLIBNAME=$(LIBNAME).$(DYLIBSUFFIX)

DYLIB_MAKE_CMD=$(CC) $(PLATFORM_FLAGS) -shared -Wl,-soname,$(DYLIB_MINOR_NAME)
STLIBNAME=$(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD=$(AR) rcs

#################### SSL variables start ####################
SSL_OBJ=ssl.o
SSL_LIBNAME=libhisider_ssl
SSL_PKGCONFNAME=hisider_ssl.pc
SSL_INSTALLNAME=install-ssl
SSL_DYLIB_MINOR_NAME=$(SSL_LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_SONAME)
SSL_DYLIB_MAJOR_NAME=$(SSL_LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR)
SSL_DYLIBNAME=$(SSL_LIBNAME).$(DYLIBSUFFIX)
SSL_STLIBNAME=$(SSL_LIBNAME).$(STLIBSUFFIX)
SSL_DYLIB_MAKE_CMD=$(CC) $(PLATFORM_FLAGS) -shared -Wl,-soname,$(SSL_DYLIB_MINOR_NAME)

USE_SSL?=0
ifeq ($(USE_SSL),1)
  # This is required for test.c only
  CFLAGS+=-DHIREDIS_TEST_SSL
  EXAMPLES+=hisider-example-ssl hisider-example-libevent-ssl
  SSL_STLIB=$(SSL_STLIBNAME)
  SSL_DYLIB=$(SSL_DYLIBNAME)
  SSL_PKGCONF=$(SSL_PKGCONFNAME)
  SSL_INSTALL=$(SSL_INSTALLNAME)
else
  SSL_STLIB=
  SSL_DYLIB=
  SSL_PKGCONF=
  SSL_INSTALL=
endif
##################### SSL variables end #####################


# Platform-specific overrides
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# This is required for test.c only
ifeq ($(TEST_ASYNC),1)
  export CFLAGS+=-DHIREDIS_TEST_ASYNC
endif

ifeq ($(USE_SSL),1)
  ifndef OPENSSL_PREFIX
    ifeq ($(uname_S),Darwin)
      SEARCH_PATH1=/opt/homebrew/opt/openssl
      SEARCH_PATH2=/usr/local/opt/openssl

      ifneq ($(wildcard $(SEARCH_PATH1)),)
        OPENSSL_PREFIX=$(SEARCH_PATH1)
      else ifneq ($(wildcard $(SEARCH_PATH2)),)
        OPENSSL_PREFIX=$(SEARCH_PATH2)
      endif
    endif
  endif

  ifdef OPENSSL_PREFIX
    CFLAGS+=-I$(OPENSSL_PREFIX)/include
    SSL_LDFLAGS+=-L$(OPENSSL_PREFIX)/lib
  endif

  SSL_LDFLAGS+=-lssl -lcrypto
endif

ifeq ($(uname_S),FreeBSD)
  LDFLAGS+=-lm
  IS_GCC=$(shell sh -c '$(CC) --version 2>/dev/null |egrep -i -c "gcc"')
  ifeq ($(IS_GCC),1)
    REAL_CFLAGS+=-pedantic
  endif
else
  REAL_CFLAGS+=-pedantic
endif

ifeq ($(uname_S),SunOS)
  IS_SUN_CC=$(shell sh -c '$(CC) -V 2>&1 |egrep -i -c "sun|studio"')
  ifeq ($(IS_SUN_CC),1)
    SUN_SHARED_FLAG=-G
  else
    SUN_SHARED_FLAG=-shared
  endif
  REAL_LDFLAGS+= -ldl -lnsl -lsocket
  DYLIB_MAKE_CMD=$(CC) $(SUN_SHARED_FLAG) -o $(DYLIBNAME) -h $(DYLIB_MINOR_NAME) $(LDFLAGS)
  SSL_DYLIB_MAKE_CMD=$(CC) $(SUN_SHARED_FLAG) -o $(SSL_DYLIBNAME) -h $(SSL_DYLIB_MINOR_NAME) $(LDFLAGS) $(SSL_LDFLAGS)
endif
ifeq ($(uname_S),Darwin)
  DYLIBSUFFIX=dylib
  DYLIB_MINOR_NAME=$(LIBNAME).$(HIREDIS_SONAME).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD=$(CC) -dynamiclib -Wl,-install_name,$(PREFIX)/$(LIBRARY_PATH)/$(DYLIB_MINOR_NAME) -o $(DYLIBNAME) $(LDFLAGS)
  SSL_DYLIB_MAKE_CMD=$(CC) -dynamiclib -Wl,-install_name,$(PREFIX)/$(LIBRARY_PATH)/$(SSL_DYLIB_MINOR_NAME) -o $(SSL_DYLIBNAME) $(LDFLAGS) $(SSL_LDFLAGS)
  DYLIB_PLUGIN=-Wl,-undefined -Wl,dynamic_lookup
endif

all: dynamic static hisider-test pkgconfig

dynamic: $(DYLIBNAME) $(SSL_DYLIB)

static: $(STLIBNAME) $(SSL_STLIB)

pkgconfig: $(PKGCONFNAME) $(SSL_PKGCONF)

# Deps (use make dep to generate this)
alloc.o: alloc.c fmacros.h alloc.h
async.o: async.c fmacros.h alloc.h async.h hisider.h read.h sds.h net.h dict.c dict.h win32.h async_private.h
dict.o: dict.c fmacros.h alloc.h dict.h
hisider.o: hisider.c fmacros.h hisider.h read.h sds.h alloc.h net.h async.h win32.h
net.o: net.c fmacros.h net.h hisider.h read.h sds.h alloc.h sockcompat.h win32.h
read.o: read.c fmacros.h alloc.h read.h sds.h win32.h
sds.o: sds.c sds.h sdsalloc.h alloc.h
sockcompat.o: sockcompat.c sockcompat.h
test.o: test.c fmacros.h hisider.h read.h sds.h alloc.h net.h sockcompat.h win32.h

$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) -o $(DYLIBNAME) $(OBJ) $(REAL_LDFLAGS)

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(STLIBNAME) $(OBJ)

#################### SSL building rules start ####################
$(SSL_DYLIBNAME): $(SSL_OBJ)
	$(SSL_DYLIB_MAKE_CMD) $(DYLIB_PLUGIN) -o $(SSL_DYLIBNAME) $(SSL_OBJ) $(REAL_LDFLAGS) $(LDFLAGS) $(SSL_LDFLAGS)

$(SSL_STLIBNAME): $(SSL_OBJ)
	$(STLIB_MAKE_CMD) $(SSL_STLIBNAME) $(SSL_OBJ)

$(SSL_OBJ): ssl.c hisider.h read.h sds.h alloc.h async.h win32.h async_private.h
#################### SSL building rules end ####################

# Binaries:
hisider-example-libevent: examples/example-libevent.c adapters/libevent.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -levent $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-libevent-ssl: examples/example-libevent-ssl.c adapters/libevent.h $(STLIBNAME) $(SSL_STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -levent $(STLIBNAME) $(SSL_STLIBNAME) $(REAL_LDFLAGS) $(SSL_LDFLAGS)

hisider-example-libev: examples/example-libev.c adapters/libev.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -lev $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-libhv: examples/example-libhv.c adapters/libhv.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -lhv $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-glib: examples/example-glib.c adapters/glib.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< $(shell pkg-config --cflags --libs glib-2.0) $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-ivykis: examples/example-ivykis.c adapters/ivykis.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -livykis $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-macosx: examples/example-macosx.c adapters/macosx.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< -framework CoreFoundation $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-ssl: examples/example-ssl.c $(STLIBNAME) $(SSL_STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< $(STLIBNAME) $(SSL_STLIBNAME) $(REAL_LDFLAGS) $(SSL_LDFLAGS)

hisider-example-poll: examples/example-poll.c adapters/poll.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< $(STLIBNAME) $(REAL_LDFLAGS)

ifndef AE_DIR
hisider-example-ae:
	@echo "Please specify AE_DIR (e.g. <sider repository>/src)"
	@false
else
hisider-example-ae: examples/example-ae.c adapters/ae.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. -I$(AE_DIR) $< $(AE_DIR)/ae.o $(AE_DIR)/zmalloc.o $(AE_DIR)/../deps/jemalloc/lib/libjemalloc.a -pthread $(STLIBNAME)
endif

ifndef LIBUV_DIR
# dynamic link libuv.so
hisider-example-libuv: examples/example-libuv.c adapters/libuv.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. -I$(LIBUV_DIR)/include $< -luv -lpthread -lrt $(STLIBNAME) $(REAL_LDFLAGS)
else
# use user provided static lib
hisider-example-libuv: examples/example-libuv.c adapters/libuv.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. -I$(LIBUV_DIR)/include $< $(LIBUV_DIR)/.libs/libuv.a -lpthread -lrt $(STLIBNAME) $(REAL_LDFLAGS)
endif

ifeq ($(and $(QT_MOC),$(QT_INCLUDE_DIR),$(QT_LIBRARY_DIR)),)
hisider-example-qt:
	@echo "Please specify QT_MOC, QT_INCLUDE_DIR AND QT_LIBRARY_DIR"
	@false
else
hisider-example-qt: examples/example-qt.cpp adapters/qt.h $(STLIBNAME)
	$(QT_MOC) adapters/qt.h -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore | \
	    $(CXX) -x c++ -o qt-adapter-moc.o -c - $(REAL_CFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore
	$(QT_MOC) examples/example-qt.h -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore | \
	    $(CXX) -x c++ -o qt-example-moc.o -c - $(REAL_CFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore
	$(CXX) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore -L$(QT_LIBRARY_DIR) qt-adapter-moc.o qt-example-moc.o $< -pthread $(STLIBNAME) -lQtCore
endif

hisider-example: examples/example.c $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< $(STLIBNAME) $(REAL_LDFLAGS)

hisider-example-push: examples/example-push.c $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) -I. $< $(STLIBNAME) $(REAL_LDFLAGS)

examples: $(EXAMPLES)

TEST_LIBS = $(STLIBNAME) $(SSL_STLIB)
TEST_LDFLAGS = $(SSL_LDFLAGS)
ifeq ($(USE_SSL),1)
  TEST_LDFLAGS += -pthread
endif
ifeq ($(TEST_ASYNC),1)
    TEST_LDFLAGS += -levent
endif

hisider-test: test.o $(TEST_LIBS)
	$(CC) -o $@ $(REAL_CFLAGS) -I. $^ $(REAL_LDFLAGS) $(TEST_LDFLAGS)

hisider-%: %.o $(STLIBNAME)
	$(CC) $(REAL_CFLAGS) -o $@ $< $(TEST_LIBS) $(REAL_LDFLAGS)

test: hisider-test
	./hisider-test

check: hisider-test
	TEST_SSL=$(USE_SSL) ./test.sh

.c.o:
	$(CC) -std=c99 -c $(REAL_CFLAGS) $<

clean:
	rm -rf $(DYLIBNAME) $(STLIBNAME) $(SSL_DYLIBNAME) $(SSL_STLIBNAME) $(TESTS) $(PKGCONFNAME) examples/hisider-example* *.o *.gcda *.gcno *.gcov

dep:
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM *.c

INSTALL?= cp -pPR

$(PKGCONFNAME): hisider.h
	@echo "Generating $@ for pkgconfig..."
	@echo prefix=$(PREFIX) > $@
	@echo exec_prefix=\$${prefix} >> $@
	@echo libdir=$(PREFIX)/$(LIBRARY_PATH) >> $@
	@echo includedir=$(PREFIX)/include >> $@
	@echo pkgincludedir=$(PREFIX)/$(INCLUDE_PATH) >> $@
	@echo >> $@
	@echo Name: hisider >> $@
	@echo Description: Minimalistic C client library for Sider. >> $@
	@echo Version: $(HIREDIS_MAJOR).$(HIREDIS_MINOR).$(HIREDIS_PATCH) >> $@
	@echo Libs: -L\$${libdir} -lhisider >> $@
	@echo Cflags: -I\$${pkgincludedir} -I\$${includedir} -D_FILE_OFFSET_BITS=64 >> $@

$(SSL_PKGCONFNAME): hisider_ssl.h
	@echo "Generating $@ for pkgconfig..."
	@echo prefix=$(PREFIX) > $@
	@echo exec_prefix=\$${prefix} >> $@
	@echo libdir=$(PREFIX)/$(LIBRARY_PATH) >> $@
	@echo includedir=$(PREFIX)/include >> $@
	@echo pkgincludedir=$(PREFIX)/$(INCLUDE_PATH) >> $@
	@echo >> $@
	@echo Name: hisider_ssl >> $@
	@echo Description: SSL Support for hisider. >> $@
	@echo Version: $(HIREDIS_MAJOR).$(HIREDIS_MINOR).$(HIREDIS_PATCH) >> $@
	@echo Requires: hisider >> $@
	@echo Libs: -L\$${libdir} -lhisider_ssl >> $@
	@echo Libs.private: -lssl -lcrypto >> $@

install: $(DYLIBNAME) $(STLIBNAME) $(PKGCONFNAME) $(SSL_INSTALL)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_INCLUDE_PATH)/adapters $(INSTALL_LIBRARY_PATH)
	$(INSTALL) hisider.h async.h read.h sds.h alloc.h sockcompat.h $(INSTALL_INCLUDE_PATH)
	$(INSTALL) adapters/*.h $(INSTALL_INCLUDE_PATH)/adapters
	$(INSTALL) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MINOR_NAME) $(DYLIBNAME) && ln -sf $(DYLIB_MINOR_NAME) $(DYLIB_MAJOR_NAME)
	$(INSTALL) $(STLIBNAME) $(INSTALL_LIBRARY_PATH)
	mkdir -p $(INSTALL_PKGCONF_PATH)
	$(INSTALL) $(PKGCONFNAME) $(INSTALL_PKGCONF_PATH)

install-ssl: $(SSL_DYLIBNAME) $(SSL_STLIBNAME) $(SSL_PKGCONFNAME)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) hisider_ssl.h $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(SSL_DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(SSL_DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(SSL_DYLIB_MINOR_NAME) $(SSL_DYLIBNAME) && ln -sf $(SSL_DYLIB_MINOR_NAME) $(SSL_DYLIB_MAJOR_NAME)
	$(INSTALL) $(SSL_STLIBNAME) $(INSTALL_LIBRARY_PATH)
	mkdir -p $(INSTALL_PKGCONF_PATH)
	$(INSTALL) $(SSL_PKGCONFNAME) $(INSTALL_PKGCONF_PATH)

32bit:
	@echo ""
	@echo "WARNING: if this fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

32bit-vars:
	$(eval CFLAGS=-m32)
	$(eval LDFLAGS=-m32)

gprof:
	$(MAKE) CFLAGS="-pg" LDFLAGS="-pg"

gcov:
	$(MAKE) CFLAGS+="-fprofile-arcs -ftest-coverage" LDFLAGS="-fprofile-arcs"

coverage: gcov
	make check
	mkdir -p tmp/lcov
	lcov -d . -c --exclude '/usr*' -o tmp/lcov/hisider.info
	lcov -q -l tmp/lcov/hisider.info
	genhtml --legend -q -o tmp/lcov/report tmp/lcov/hisider.info

noopt:
	$(MAKE) OPTIMIZATION=""

.PHONY: all test check clean dep install 32bit 32bit-vars gprof gcov noopt
