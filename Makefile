# Platform library Makefile for Linux and macOS
# Builds a dynamic library (.so on Linux, .dylib on macOS)

# Detect operating system
UNAME_S := $(shell uname -s)

# Library name
LIB_NAME = platform

# Compiler settings
ifeq ($(UNAME_S),Darwin)
    # macOS settings
    CC = clang
    OBJC = clang
    CFLAGS = -Wall -Wextra -fPIC -I.
    OBJCFLAGS = -Wall -Wextra -fPIC -I. -fobjc-arc
    LDFLAGS = -dynamiclib -framework AppKit -framework Cocoa
    LIB_EXT = dylib
    PLATFORM_SOURCES = macos/macos_event.m macos/macos_menu.m macos/macos_system.m macos/macos_window.m \
                       unix/unix_net.c unix/unix_shared.c
else ifeq ($(UNAME_S),Linux)
    # Linux settings
    CC = gcc
    CFLAGS = -Wall -Wextra -fPIC -I.
    LDFLAGS = -shared
    LIB_EXT = so
    
    # Check for Wayland support
    WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client wayland-egl xkbcommon egl gl 2>/dev/null)
    WAYLAND_LIBS := $(shell pkg-config --libs wayland-client wayland-egl xkbcommon egl gl 2>/dev/null)
    
    ifneq ($(WAYLAND_CFLAGS),)
        CFLAGS += $(WAYLAND_CFLAGS)
        LDFLAGS += $(WAYLAND_LIBS)
        PLATFORM_SOURCES = wayland/wayland_egl.c wayland/wayland_event.c wayland/wayland_system.c \
                          wayland/wayland_window.c wayland/xdg-shell-client.c \
                          unix/unix_net.c unix/unix_shared.c
    else
        $(warning Wayland development libraries not found. Install with: sudo apt-get install libwayland-dev libwayland-egl1 libxkbcommon-dev libegl-dev)
        PLATFORM_SOURCES = unix/unix_net.c unix/unix_shared.c
    endif
else
    $(error Unsupported operating system: $(UNAME_S))
endif

# Output library
LIB_TARGET = lib$(LIB_NAME).$(LIB_EXT)

# Build object files from source files
OBJECTS = $(patsubst %.c,%.o,$(patsubst %.m,%.o,$(PLATFORM_SOURCES)))

# Default target
.PHONY: all
all: $(LIB_TARGET)

# Build the dynamic library
$(LIB_TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Build complete: $@"

# Compile C source files
%.o: %.c platform.h
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Objective-C source files (macOS only)
%.o: %.m platform.h
	@echo "Compiling $<..."
	$(OBJC) $(OBJCFLAGS) -c $< -o $@

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(LIB_TARGET)
	rm -f macos/*.o unix/*.o wayland/*.o qnx/*.o
	@echo "Clean complete."

# Install library (optional)
.PHONY: install
install: $(LIB_TARGET)
	@echo "Installing $(LIB_TARGET) to /usr/local/lib..."
	@install -d /usr/local/lib
	@install -m 755 $(LIB_TARGET) /usr/local/lib/
	@install -d /usr/local/include
	@install -m 644 platform.h /usr/local/include/
	@echo "Installation complete."

# Uninstall library (optional)
.PHONY: uninstall
uninstall:
	@echo "Uninstalling $(LIB_TARGET) from /usr/local/lib..."
	rm -f /usr/local/lib/$(LIB_TARGET)
	rm -f /usr/local/include/platform.h
	@echo "Uninstall complete."

# Help target
.PHONY: help
help:
	@echo "Platform Library Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the dynamic library (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install library to /usr/local/lib"
	@echo "  uninstall  - Remove library from /usr/local/lib"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Current platform: $(UNAME_S)"
	@echo "Library target: $(LIB_TARGET)"
