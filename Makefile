.PHONY: debug release all

SOURCE_FILES = main.c vk.c

CC = clang
CFLAGS = -std=c17 -O0 -g -xc
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

FILENAME = release

all:
	$(MAKE) release
	$(MAKE) debug

release:
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(SOURCE_FILES) -o $(FILENAME).out

debug: EXTRA_FLAGS = -DDEBUG
debug: FILENAME = debug
debug: release
