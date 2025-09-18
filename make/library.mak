LIBRARY_NAMES := vk general model window

OBJECTS_FOLDER := $(BUILD_FOLDER)/objects
LIBRARY_FOLDER := $(BUILD_FOLDER)/lib

COMPONENTS := $(foreach lib,$(LIBRARY_NAMES),$(patsubst $(SOURCE_FOLDER)/$(lib)/%.c,$(lib)/$(PREFIX)$(lib)_%,$(wildcard $(SOURCE_FOLDER)/$(lib)/*.c)))

OBJECTS_RELEASE := $(foreach c,$(COMPONENTS),$(OBJECTS_FOLDER)/$c$(OBJ_EXT))
OBJECTS_DEBUG := $(patsubst %$(OBJ_EXT),%$(DEBUG_SUFFIX)$(OBJ_EXT),$(OBJECTS_RELEASE))

$(foreach lib,$(LIBRARY_NAMES),$(eval $(lib)_OBJ_REL := $(filter $(OBJECTS_FOLDER)/$(lib)/%,$(OBJECTS_RELEASE))))
$(foreach lib,$(LIBRARY_NAMES),$(eval $(lib)_OBJ_DBG := $(filter $(OBJECTS_FOLDER)/$(lib)/%,$(OBJECTS_DEBUG))))

$(LIBRARY_FOLDER):
	mkdir -p $@

$(MODULES_DIR)/%:
	./scripts/init-submodules.sh

$(MODULES_DIR)/fpxlibc/build/lib/%$(LIB_EXT): | $(MODULES_DIR)/fpxlibc
	cd $|; $(MAKE) $(subst $(MODULES_DIR)/fpxlibc/,,$@)

MODEL_DEPS := serialize c-utils alloc mem math
MODEL_DEPS := $(foreach dep,$(MODEL_DEPS),$(MODULES_DIR)/fpxlibc/build/lib/libfpx_$(dep)$(LIB_EXT))

# for the model library
$(LIBRARY_FOLDER)/$(LIB_PREFIX)model$(LIB_EXT): $(MODEL_DEPS)
$(LIBRARY_FOLDER)/$(LIB_PREFIX)model$(DEBUG_SUFFIX)$(LIB_EXT): $(subst $(LIB_EXT),$(DEBUG_SUFFIX)$(LIB_EXT),$(MODEL_DEPS))

define new-lib-target

LIBS_RELEASE += $(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(LIB_EXT)
$(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(LIB_EXT): $($(1)_OBJ_REL) | $(LIBRARY_FOLDER)
	-if [ -f $$@ ]; then rm $$@; fi
	$(AR) cr --thin $$@ $$^ && echo -e 'create $$@\naddlib $$@\nsave\nend' | ar -M

LIBS_DEBUG += $(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(DEBUG_SUFFIX)$(LIB_EXT)
$(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(DEBUG_SUFFIX)$(LIB_EXT): $($(1)_OBJ_DBG) | $(LIBRARY_FOLDER)
	-if [ -f $$@ ]; then rm $$@; fi
	$(AR) cr --thin $$@ $$^ && echo -e 'create $$@\naddlib $$@\nsave\nend' | ar -M

endef

$(foreach lib,$(LIBRARY_NAMES),$(eval $(call new-lib-target,$(lib))))
