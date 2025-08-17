MINGW_DLLS := libwinpthread-1.dll
GLFW_DLL := glfw3.dll

REQUIRED_DLLS := $(MINGW_DLLS) $(GLFW_DLL)

find_pipe := head -n 1 | xargs -I found ln -s found .

$(MINGW_DLLS):
	find $(MINGW_DIRECTORY) -name "$@" | $(find_pipe)
$(GLFW_DLL):
	find $(GLFW_LIBRARY_DIRECTORY) -name "$@" | $(find_pipe)
