# POSIX build of the object simple_vulkan.ecf expects on macOS and Linux.
# Usage: make -C Clib    (Vulkan headers from Homebrew on macOS, the system on Linux)
CC ?= cc
VULKAN_PREFIX ?= $(shell [ -d /opt/homebrew/include/vulkan ] && echo /opt/homebrew)
CFLAGS ?= -O2 -fPIC $(if $(VULKAN_PREFIX),-I$(VULKAN_PREFIX)/include)

simple_vulkan.o: simple_vulkan.c simple_vulkan.h
	$(CC) $(CFLAGS) -c simple_vulkan.c -o simple_vulkan.o

clean:
	rm -f simple_vulkan.o

.PHONY: clean
