CC      = gcc
CFLAGS  = -Wall -Werror -std=gnu11 -Wno-deprecated-declarations -DVERSION=\"2.0.1\"
LDFLAGS =

PKG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
PKG_LIBS   = $(shell pkg-config --libs gtk+-3.0)

SRCS    = gensystray.c gensystray_config_parser.c gensystray_config_monitor.c
TARGET  = gensystray
OSX_OBJ = gensystray_osx_menu.o

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

$(TARGET): deps_check $(SRCS) $(OSX_OBJ) $(UCL_OBJS) $(SS_OBJ)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(UCL_INC) $(SS_INC) -o $@ $(filter-out deps_check,$^) $(LDFLAGS) $(PKG_LIBS) -lm \
		-Wl,-framework,Cocoa

deps_check:
	@test -f $(UCL_DIR)/include/ucl.h || (echo "error: submodules not initialized, run: make init" && exit 1)
	@test -f $(SS_DIR)/include/ss_lib.h || (echo "error: submodules not initialized, run: make init" && exit 1)

$(OSX_OBJ): gensystray_osx_menu.m
	clang -fobjc-arc $(PKG_CFLAGS) -c -o $@ $<

$(UCL_DIR)/src/%.o: $(UCL_DIR)/src/%.c
	$(CC) $(UCL_CFLAGS) -c -o $@ $<

$(SS_DIR)/src/ss_lib.o: $(SS_DIR)/src/ss_lib.c
	$(CC) -std=gnu11 -w -I$(SS_DIR)/include -c -o $@ $<

init:
	git submodule update --init --recursive

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OSX_OBJ) $(UCL_OBJS) $(SS_OBJ)

.PHONY: init run clean deps_check
