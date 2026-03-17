CC      = gcc
CFLAGS  = -Wall -Werror -std=c11 -Wno-deprecated-declarations
LDFLAGS =

PKG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 sdl2)
PKG_LIBS   = $(shell pkg-config --libs gtk+-3.0 sdl2)

SRCS   = gensystray.c gensystray_utils.c gensystray_config_parser.c gensystray_config_monitor.c dlist.c
TARGET = gensystray

ifdef DEBUG
  CFLAGS += -g -O0
else
  CFLAGS += -O2
endif

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ $^ $(LDFLAGS) $(PKG_LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
