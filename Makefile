.PHONY: debug build

CC = clang
CFLAGS = -std=c17 -Og -g
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

build:
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) main.c -o vulkan.out

debug: EXTRA_FLAGS = -DDEBUG
debug: build
