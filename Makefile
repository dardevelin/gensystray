CC      = gcc
CFLAGS  = -Wall -Werror -std=gnu11 -Wno-deprecated-declarations -DVERSION=\"2.1.0\"
LDFLAGS =

PKG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
PKG_LIBS   = $(shell pkg-config --libs gtk+-3.0)

SRCS    = gensystray.c gensystray_config_parser.c gensystray_config_monitor.c
TARGET  = gensystray

# platform detection — macOS uses ObjC NSEvent monitor, Linux uses no-op stub
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  PLATFORM_SRC = gensystray_osx_menu.m
  PLATFORM_OBJ = gensystray_osx_menu.o
  PLATFORM_LDFLAGS = -Wl,-framework,Cocoa
else
  PLATFORM_SRC = gensystray_linux_menu.c
  PLATFORM_OBJ = gensystray_linux_menu.o
  PLATFORM_LDFLAGS =
endif

# ss_lib (signal-slot library, built from submodule)
SS_DIR  = deps/ss_lib
SS_INC  = -I$(SS_DIR)/include
SS_OBJ  = $(SS_DIR)/src/ss_lib.o

# libucl (built from submodule sources, no install needed)
UCL_DIR    = deps/libucl
UCL_INC    = -I$(UCL_DIR)/include -I$(UCL_DIR)/src -I$(UCL_DIR)/uthash -I$(UCL_DIR)/klib
UCL_CFLAGS = $(UCL_INC) -std=gnu11 -w
UCL_SRCS   = $(UCL_DIR)/src/ucl_util.c \
             $(UCL_DIR)/src/ucl_parser.c \
             $(UCL_DIR)/src/ucl_emitter.c \
             $(UCL_DIR)/src/ucl_emitter_utils.c \
             $(UCL_DIR)/src/ucl_emitter_streamline.c \
             $(UCL_DIR)/src/ucl_hash.c \
             $(UCL_DIR)/src/ucl_msgpack.c \
             $(UCL_DIR)/src/ucl_sexp.c
UCL_OBJS   = $(UCL_SRCS:.c=.o)

ifdef DEBUG
  CFLAGS += -g -O0
else
  CFLAGS += -O2
endif

$(TARGET): deps_check $(SRCS) $(PLATFORM_OBJ) $(UCL_OBJS) $(SS_OBJ)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(UCL_INC) $(SS_INC) -o $@ $(filter-out deps_check,$^) $(LDFLAGS) $(PKG_LIBS) -lm \
		$(PLATFORM_LDFLAGS)

deps_check:
	@test -f $(UCL_DIR)/include/ucl.h || (echo "submodules not found, running make init..." && $(MAKE) init)
	@test -f $(SS_DIR)/include/ss_lib.h || (echo "submodules not found, running make init..." && $(MAKE) init)

gensystray_osx_menu.o: gensystray_osx_menu.m
	clang -fobjc-arc $(PKG_CFLAGS) $(LTO_FLAGS) -c -o $@ $<

gensystray_linux_menu.o: gensystray_linux_menu.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(LTO_FLAGS) -c -o $@ $<

$(UCL_DIR)/src/%.o: $(UCL_DIR)/src/%.c
	$(CC) $(UCL_CFLAGS) $(LTO_FLAGS) -c -o $@ $<

$(SS_DIR)/src/ss_lib.o: $(SS_DIR)/src/ss_lib.c
	$(CC) -std=gnu11 -w -I$(SS_DIR)/include $(LTO_FLAGS) -c -o $@ $<

RELEASE_CFLAGS = -flto=thin -Os -ffunction-sections -fdata-sections
ifeq ($(UNAME_S),Darwin)
  RELEASE_LDFLAGS = -flto=thin -Wl,-dead_strip
else
  RELEASE_LDFLAGS = -flto=thin -Wl,--gc-sections
endif

release: LTO_FLAGS = -flto=thin
release: CFLAGS += $(RELEASE_CFLAGS)
release: LDFLAGS += $(RELEASE_LDFLAGS)
release: clean $(TARGET)
	strip $(TARGET)
	@ls -lh $(TARGET) | awk '{print "release binary: " $$5}'

init:
	git submodule update --init --recursive

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) gensystray_osx_menu.o gensystray_linux_menu.o $(UCL_OBJS) $(SS_OBJ)

.PHONY: init run clean deps_check release
