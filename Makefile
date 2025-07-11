.PHONY: debug release all compile clean clean_libs shaders

CC = clang
CFLAGS = -std=c17
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

all:
	$(MAKE) release
	$(MAKE) debug

release: EXTRA_FLAGS = -O1
release: FILENAME = release
release: clean_libs main

debug: EXTRA_FLAGS = -DDEBUG -O0 -g
debug: FILENAME = debug
debug: clean_libs main

vk.o:
	$(CC) $(CFLAGS) -c $(EXTRA_FLAGS) vk.c -o $@

main: vk.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) main.c $^ -o $(FILENAME).out

clean_libs:
	rm *.o 2>/dev/null || true

clean: clean_libs
	rm *.out 2>/dev/null || true

shaders:
	bash -c './compile_shaders.sh'
