# fpxlib3d

heeheehee

## Table of Contents
1. [Note regarding GPU vendors](#note_gpu)
2. [Build from source](#build_from_source)
    1. [General information](#build_general)
    2. [Arch Linux](#build_arch)
    3. [Microsoft Windows](#build_windows)

## NOTE: <a name="note_gpu"></a>
NVIDIA on Wayland has known issues.
AMD on Wayland is fine, except AMD doesn't care about an outdated swapchain frame.
Either way, NVIDIA on X11 works fully as intended

## Build from source <a name="build_from_source"></a>

### General information <a name="build_general"></a>

#### Makefile targets <a name="build_make_targets"></a>
Within the root directory of the source tree, run `make` to compile the object files of the library. Run `make test` to also compile and link `src/main.c` for testing.

! Make sure to run `make clean` before switching build targets (Windows, Linux) !

#### Makefile variables <a name="build_make_vars"></a>
Some Makefile variables you can define to influence the compilation and linkage are:
- WINDOWS | set to `true` to compile to a dynamically linked Windows PE. See [Microsoft Windows - Cross-compilation](#build_windows_cross) for details.

These are for directories. Set these to wherever the respective file or files are kept
- VULKAN_INCLUDE_DIRECTORY
- GLFW_INCLUDE_DIRECTORY
- CGLM_INCLUDE_DIRECTORY
- VULKAN_LIBRARY_DIRECTORY
- GLFW_LIBRARY_DIRECTORY


### Arch Linux <a name="build_arch"></a>

#### Prerequisites
```shell
sudo pacman -S base-devel vulkan-devel glfw shaderc
```

#### Optional dependencies
```shell
sudo pacman -S clang
```
You will also need [cglm](https://github.com/recp/cglm) which is also available on the [AUR](https://aur.archlinux.org/packages/cglm)

#### Compiling and linking
See: [Makefile targets](#build_make_targets)


### Microsoft Windows <a name="build_windows"></a>
While this project is cross-compilable from Linux to Windows, compilation on Windows is not yet supported.

#### Cross-compilation <a name="build_windows_cross"></a>
Install the `mingw-w64` toolchain packaged by your Linux distribution of choice. You can set up your include-directories and other optional Makefile variables by creating a `.mk` file in the make/ directory and defining variables See: [Makefile variables](#build_make_vars). Afterwards you run your make-command of choice. See: [Makefile targets](#build_make_targets)
