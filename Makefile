UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	CC = clang
	CFLAGS = -Wall -Wextra -fPIC -I. -DGL_SILENCE_DEPRECATION
	LDFLAGS = -dynamiclib -framework AppKit -framework Cocoa -framework OpenGL -framework IOSurface
	LIB_EXT = dylib
	SOURCES = $(wildcard macos/*.m) $(wildcard unix/*.c)
	LDFLAGS += -dynamiclib -install_name @rpath/libplatform.dylib
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
		SOURCES = $(wildcard wayland/*.c) $(wildcard unix/*.c)
	else
		SOURCES = $(wildcard unix/*.c)
	endif
else
	$(error Unsupported OS: $(UNAME_S))
endif

OUTDIR ?= .
LIBNAME = libplatform.$(LIB_EXT)
TARGET = $(OUTDIR)/$(LIBNAME)
OBJECTS = $(patsubst %.m,%.o,$(patsubst %.c,%.o,$(SOURCES)))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.m
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) */*.o

install: $(TARGET)
	install -d /usr/local/lib /usr/local/include
	install -m 755 $(TARGET) /usr/local/lib/
	install -m 644 platform.h /usr/local/include/

.PHONY: all clean install
