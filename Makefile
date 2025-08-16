.PHONY: all prep release debug libs test clean shaders

# NOTE: remember to `make clean` after switching target OS
# WINDOWS := true

CC != which clang 2>/dev/null
CC_WIN32 := x86_64-w64-mingw32-gcc

include make/early.mak

WINDOWS_TARGET_NAME := win64

DEBUG_SUFFIX := _debug

ifeq ($(WINDOWS), true)

	TARGET := win64

	-include make/windows/*.mk

	CC := $(CC_WIN32)
	CFLAGS += -mwindows -DVK_USE_PLATFORM_WIN32_KHR
	LDFLAGS += -lglfw3
	# LDFLAGS += -lwinpthread-1
	
	EXE_EXT := .exe
	OBJ_EXT := .obj

else

	TARGET := linux

ifeq ($(CC),)
	CC != which cc
endif

	LDFLAGS += -lglfw
	
	# EXE_EXT := .out
	OBJ_EXT := .o

endif

include make/*.mk

EXE_EXT := $(TARGET)$(EXE_EXT)

LIB_OBJECTS := vk

include make/variables.mak

all: libs shaders

clean:
	rm -rf ./$(BUILD_FOLDER) || true

shaders: $(SHADER_FILES)

release: $(OBJECTS_RELEASE)
debug: $(OBJECTS_DEBUG)

# testing app
test: $(RELEASE_APP) $(DEBUG_APP) $(SHADER_FILES)

# individual libraries, both RELEASE and DEBUG
libs: $(OBJECTS_RELEASE) $(OBJECTS_DEBUG)

$(OBJECTS_FOLDER):
	mkdir -p $@

$(OBJECTS_RELEASE): $(OBJECTS_FOLDER)/%$(OBJ_EXT): $(SOURCE_FOLDER)/%.c | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -c $< -o $@

$(OBJECTS_DEBUG): $(OBJECTS_FOLDER)/%$(DEBUG_SUFFIX)$(OBJ_EXT): $(SOURCE_FOLDER)/%.c | $(OBJECTS_FOLDER)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) -c $< -o $@

$(RELEASE_APP): LDFLAGS += -s
$(RELEASE_APP): $(OBJECTS_RELEASE) $(MAIN_C)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(RELEASE_FLAGS) $^ -o $@

$(DEBUG_APP): $(OBJECTS_DEBUG) $(MAIN_C)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(DEBUG_FLAGS) $^ -o $@

$(SHADER_FILES): %.spv: %
	glslc $< -o $@

include make/archive.mak
