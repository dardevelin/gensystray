CC      = gcc
CFLAGS  = -Wall -Werror -std=gnu11 -Wno-deprecated-declarations
LDFLAGS =

PKG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
PKG_LIBS   = $(shell pkg-config --libs gtk+-3.0)

SRCS   = gensystray.c gensystray_config_parser.c gensystray_config_monitor.c
TARGET = gensystray

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

$(UCL_DIR)/src/%.o: $(UCL_DIR)/src/%.c
	$(CC) $(UCL_CFLAGS) -c -o $@ $<

$(TARGET): $(SRCS) $(UCL_OBJS)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(UCL_INC) -o $@ $^ $(LDFLAGS) $(PKG_LIBS) -lm

init:
	git submodule update --init --recursive

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(UCL_OBJS)

.PHONY: init run clean
