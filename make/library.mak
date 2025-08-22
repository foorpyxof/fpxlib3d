$(foreach lib,$(LIBRARY_NAMES),$(eval $(lib)_OBJ_REL := $(filter $(OBJECTS_FOLDER)/$(lib)/%,$(OBJECTS_RELEASE))))
$(foreach lib,$(LIBRARY_NAMES),$(eval $(lib)_OBJ_DBG := $(filter $(OBJECTS_FOLDER)/$(lib)/%,$(OBJECTS_DEBUG))))

$(LIBRARY_FOLDER):
	mkdir -p $@

define new-lib-target
LIBS_RELEASE += $(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(LIB_EXT)
$(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(LIB_EXT): $($(1)_OBJ_REL) | $(LIBRARY_FOLDER)
	$(AR) r $$@ $$?

LIBS_DEBUG += $(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(DEBUG_SUFFIX)$(LIB_EXT)
$(LIBRARY_FOLDER)/$(LIB_PREFIX)$(1)$(DEBUG_SUFFIX)$(LIB_EXT): $($(1)_OBJ_DBG) | $(LIBRARY_FOLDER)
	$(AR) r $$@ $$?

endef

$(foreach lib,$(LIBRARY_NAMES),$(eval $(call new-lib-target,$(lib))))
