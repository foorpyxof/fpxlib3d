.PHONY: all prep release debug libs main clean shaders

CC = clang
CFLAGS = -std=c99
LDFLAGS = -lglfw -lvulkan

PREFIX = fpx

BUILD_FOLDER = build

SOURCE_FOLDER = src
INCLUDE_DIRS = include modules

CFLAGS += $(foreach dir,$(INCLUDE_DIRS),-I./$(dir))

LIB_SOURCES = vk.c

SHADER_DIR = shaders
SHADER_SRC = default.vert default.frag

all:
	$(MAKE) release
	$(MAKE) debug

prep:
	mkdir -p build/objects

release: EXTRA_FLAGS = -O1
release: FILENAME = release
release: main

debug: EXTRA_FLAGS = -DDEBUG -O0 -g
debug: FILENAME = debug
debug: main

libs: prep $(LIB_SOURCES)

$(LIB_SOURCES):
	$(CC) \
		$(CFLAGS) $(EXTRA_FLAGS) \
		-c $(SOURCE_FOLDER)/$@ \
		-o $(BUILD_FOLDER)/objects/$(PREFIX)_$(@:.c=.o)

.PHONY: $(LIB_SOURCES)

main: libs
	$(CC) \
		$(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) \
		$(SOURCE_FOLDER)/main.c \
		$(BUILD_FOLDER)/objects/*.o \
		-o $(FILENAME).out

clean:
	rm -rf ./$(BUILD_FOLDER) || true
	rm *.out || true

shaders: $(SHADER_SRC)

$(SHADER_SRC):
	glslc $(SHADER_DIR)/$@ -o $(SHADER_DIR)/$@.spv

.PHONY: $(SHADER_SRC)
