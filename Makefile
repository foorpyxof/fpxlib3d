.PHONY: debug release all compile clean

CC = clang
CFLAGS = -std=c17
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

all:
	$(MAKE) release
	$(MAKE) debug

release: EXTRA_FLAGS = -O3
release: FILENAME = release
release: main

debug: EXTRA_FLAGS = -DDEBUG -Og -g
debug: FILENAME = debug
debug: main

vk.o:
	$(CC) $(CFLAGS) -c $(EXTRA_FLAGS) vk.c -o $@

main: vk.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) main.c $^ -o $(FILENAME).out

clean:
	rm *.o 2>/dev/null || true
	rm *.out 2>/dev/null || true
