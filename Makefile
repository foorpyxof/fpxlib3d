CC = clang
CFLAGS = -std=c17 -O2
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

build:
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o vulkan.out
