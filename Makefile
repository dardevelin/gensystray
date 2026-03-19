CC      = gcc
CFLAGS  = -Wall -Werror -std=gnu11 -Wno-deprecated-declarations -DVERSION=\"2.4.0\"
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

ifeq ($(UNAME_S),Darwin)
  LTO_FLAG = -flto=thin
else
  LTO_FLAG = -flto
endif

RELEASE_CFLAGS = $(LTO_FLAG) -Os -ffunction-sections -fdata-sections
ifeq ($(UNAME_S),Darwin)
  RELEASE_LDFLAGS = $(LTO_FLAG) -Wl,-dead_strip
else
  RELEASE_LDFLAGS = $(LTO_FLAG) -Wl,--gc-sections
endif

release: LTO_FLAGS = $(LTO_FLAG)
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
	rm -rf GenSysTray.app

ifeq ($(UNAME_S),Darwin)
APP_NAME    = GenSysTray
APP_BUNDLE  = $(APP_NAME).app
APP_BINARY  = $(APP_BUNDLE)/Contents/MacOS/$(TARGET)
APP_RES     = $(APP_BUNDLE)/Contents/Resources
APP_PLIST   = $(APP_BUNDLE)/Contents/Info.plist
BUNDLE_ID   = org.gensystray.app
VERSION     = $(shell grep -o 'VERSION=\\"[^"]*\\"' Makefile | head -1 | sed 's/VERSION=\\"//;s/\\"//')

app: release
	@echo "building $(APP_BUNDLE)..."
	@mkdir -p $(APP_BUNDLE)/Contents/MacOS $(APP_RES)
	@cp $(TARGET) $(APP_BINARY)
	@cp gensystray_default.png $(APP_RES)/
	@cp assets/icons/gensystray.icns $(APP_RES)/
	@printf '<?xml version="1.0" encoding="UTF-8"?>\n\
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n\
<plist version="1.0">\n\
<dict>\n\
	<key>CFBundleIdentifier</key>      <string>$(BUNDLE_ID)</string>\n\
	<key>CFBundleName</key>            <string>$(APP_NAME)</string>\n\
	<key>CFBundleExecutable</key>      <string>$(TARGET)</string>\n\
	<key>CFBundleIconFile</key>        <string>gensystray</string>\n\
	<key>CFBundleVersion</key>         <string>2.4.0</string>\n\
	<key>CFBundleShortVersionString</key> <string>2.4.0</string>\n\
	<key>CFBundlePackageType</key>     <string>APPL</string>\n\
	<key>CFBundleSignature</key>       <string>????</string>\n\
	<key>LSUIElement</key>             <true/>\n\
	<key>NSHighResolutionCapable</key> <true/>\n\
</dict>\n\
</plist>\n' > $(APP_PLIST)
	@echo "built $(APP_BUNDLE)"

sign: app
	@test -n "$(SIGN_ID)" || (echo "error: set SIGN_ID to your Developer ID, e.g. make sign SIGN_ID=\"Developer ID Application: Name (TEAMID)\"" && exit 1)
	codesign --deep --force --options runtime --sign "$(SIGN_ID)" $(APP_BUNDLE)
	@echo "signed $(APP_BUNDLE)"

notarize: sign
	@test -n "$(APPLE_ID)"   || (echo "error: set APPLE_ID"   && exit 1)
	@test -n "$(APPLE_TEAM)" || (echo "error: set APPLE_TEAM" && exit 1)
	@test -n "$(APPLE_PASS)" || (echo "error: set APPLE_PASS (app-specific password)" && exit 1)
	ditto -c -k --keepParent $(APP_BUNDLE) $(APP_NAME).zip
	xcrun notarytool submit $(APP_NAME).zip \
		--apple-id "$(APPLE_ID)" \
		--team-id "$(APPLE_TEAM)" \
		--password "$(APPLE_PASS)" \
		--wait
	xcrun stapler staple $(APP_BUNDLE)
	rm -f $(APP_NAME).zip
	@echo "notarized and stapled $(APP_BUNDLE)"

.PHONY: init run clean deps_check release app sign notarize
else
.PHONY: init run clean deps_check release
endif
