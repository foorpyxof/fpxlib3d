OBJECTS_FOLDER = $(BUILD_FOLDER)/objects
OBJECTS_BASE = $(foreach obj,$(LIB_OBJECTS),$(OBJECTS_FOLDER)/$(obj))

OBJECTS_RELEASE = $(foreach base,$(OBJECTS_BASE),$(base).o)
OBJECTS_DEBUG = $(foreach base,$(OBJECTS_BASE),$(base)_debug.o)
