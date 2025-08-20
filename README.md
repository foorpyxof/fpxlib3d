# fpxlib3d

heeheehee

## Table of Contents
1. [Note regarding GPU vendors](#note_gpu)
2. [Build from source](#build_from_source)
    1. [General information](#build_general)
    2. [Arch Linux](#build_arch)
    3. [Microsoft Windows](#build_windows)
3. [Known bugs](#known_issues)


## 1. NOTE: <a name="note_gpu"></a>
NVIDIA on Wayland has known issues.
AMD on Wayland is fine, except AMD doesn't care about an outdated swapchain frame.
Either way, NVIDIA on X11 works fully as intended


## 2. Build from source <a name="build_from_source"></a>

### 2.1. General information <a name="build_general"></a>

#### 2.1.1. General prerequisites <a name="build_general_pre"></a>
Make sure to:
- Pull the git submodules before building
```shell
git submodule update --init --recursive
```

#### 2.1.2. Makefile targets <a name="build_make_targets"></a>
Within the root directory of the source tree, run `make -j$(nproc)` to compile the object files of the library. Run `make test -j$(nproc)` to also compile and link `src/main.c` for testing.

! Make sure to run `make clean` before switching build targets (Windows, Linux) !

#### 2.1.3. Makefile variables <a name="build_make_vars"></a>
Some Makefile variables you can define to influence the compilation and linkage are:
- `WINDOWS := true` to compile into a dynamically linked Windows PE. See [Microsoft Windows - Cross-compilation](#build_windows_cross) for details.

More variables can be set to specify- for example- library paths. In practice, the format for such a variable follows the following pattern: `*_[INCLUDE/LIBRARY]_DIRECTORY`.
For example:
- GLFW_INCLUDE_DIRECTORY := /path/to/GLFW/headers/directory
- GLFW_LIBRARY_DIRECTORY := /path/to/GLFW/lib/directory

Take a look at `make/variables.mak` as for an idea as to how this works

Keep in mind that these often won't need to be set if you're compiling on Linux, for Linux. This is because Linux is cool and the fact that package managers exist make sure that a default path specified in the compiler is usually used.


### 2.2. Arch Linux <a name="build_arch"></a>

#### 2.2.1. Prerequisites
See also: [General prerequisites](#build_general_pre)
```shell
sudo pacman -S base-devel vulkan-devel glfw shaderc
```

#### 2.2.2. Optional dependencies
```shell
sudo pacman -S clang
```

#### 2.2.3. Compiling and linking
See: [Makefile targets](#build_make_targets)


### 2.3. Microsoft Windows <a name="build_windows"></a>
While this project is cross-compilable from Linux to Windows, compilation on Windows is not yet supported.

#### 2.3.1. Cross-compilation <a name="build_windows_cross"></a>
Install the `mingw-w64` toolchain packaged by your Linux distribution of choice. You will also likely need to set up some Makefile variables (See: [Makefile variables](#build_make_vars)). Afterwards you run your make command of choice. See: [Makefile targets](#build_make_targets). After all is done, put a copy of glfw3.dll into the root directory (this may be a symlink) before running the packaging script.


## 3. Known issues <a name="known_issues"></a>
- When resizing a Vulkan Window too quickly and for too long, an error will occur stating that a new Swapchain could not be created. A crash will subsequently occur. This issue has been recorded on X11, using NVIDIA's 580.76.05-3 drivers.
