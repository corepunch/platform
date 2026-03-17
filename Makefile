OUTDIR ?= .
LIBNAME = libplatform.$(LIB_EXT)
TARGET = $(OUTDIR)/$(LIBNAME)
UNAME_S := $(shell uname -s)
HASH := \#

ifdef EMSCRIPTEN
	CC = emcc
	CFLAGS = -Wall -Wextra -I. -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=2
	LDFLAGS = -sSIDE_MODULE=1
	LIB_EXT = wasm
	FIND_SOURCES = find webgl -name "*.c"
	LANG = c
else ifeq ($(UNAME_S),Darwin)
	CC = clang
	CFLAGS = -Wall -Wextra -fPIC -I. -DGL_SILENCE_DEPRECATION
	LDFLAGS = -dynamiclib -framework AppKit -framework Cocoa -framework OpenGL -framework IOSurface -install_name @rpath/$(LIBNAME)
	LIB_EXT = dylib
	FIND_SOURCES = ( find macos -name "*.m"; find unix -name "*.c"; )
	LANG = objective-c
else ifeq ($(UNAME_S),Linux)
	CC = gcc
	CFLAGS = -Wall -Wextra -fPIC -I.
	LDFLAGS = -shared
	LIB_EXT = so
	# Try to detect Wayland libraries
	WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client wayland-egl xkbcommon egl gl 2>/dev/null)
	ifneq ($(WAYLAND_CFLAGS),)
		CFLAGS += $(WAYLAND_CFLAGS)
		LDFLAGS += $(shell pkg-config --libs wayland-client wayland-egl xkbcommon egl gl)
		FIND_SOURCES = ( find wayland -name "*.c"; find unix -name "*.c"; )
	else
		FIND_SOURCES = find unix -name "*.c"
	endif
	LANG = c
else
	$(error Unsupported OS: $(UNAME_S))
endif

all: $(TARGET)

$(TARGET):
	$(FIND_SOURCES) | sed 's|.*|$(HASH)include "&"|' | $(CC) $(CFLAGS) -x $(LANG) - $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d /usr/local/lib /usr/local/include
	install -m 755 $(TARGET) /usr/local/lib/
	install -m 644 platform.h /usr/local/include/

.PHONY: all clean install
