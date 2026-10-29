This is Vulkan version of [gl-gsync-demo](https://github.com/dahenry/gl-gsync-demo) 

This demo can be used to verify if the VRR (GSync) is working. The it is using SDL to
create a fullscreen window. It's meant to run on X11 and not Wayland. 

The position of the rectangle is delivered to the vertex shader through push constant. The framerate 
changes autmatically in range from 30 to <max_refresh_rate>.

Rendering uses Vulkan 1.4 dynamic rendering (`vkCmdBeginRendering`) and
`VK_EXT_shader_object` instead of a render pass and a graphics pipeline. 

Shaders are embedded into the binary. It was possible thanks to [bin2header](https://github.com/AntumDeluge/bin2header) 

The application was tested on Ubuntu 24.10 but it should also work on different Ubuntu versions
and/or distros.

Propertiary NVIDIA driver is probably also needed. I did not tested it with NVK.

## Dependencies

fmt 12.2.0 and SDL 3.4.18 are vendored as git submodules under `deps/`. After a fresh clone:

```
git submodule update --init --recursive
```

Vulkan is the remaining external dependency (loader + headers).

Linux:

```
sudo apt install cmake ninja-build build-essential pkg-config libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libudev-dev libdbus-1-dev libdecor-0-dev
```

Windows:

* CMake 4+ and Ninja
* A C++20 compiler (Visual Studio 2022 with the x64 toolchain, or MinGW)
* LunarG Vulkan SDK, with `VULKAN_SDK` set in the environment

The Linux build still creates the Vulkan surface through the existing X11/Wayland path. The Windows build uses `VK_KHR_win32_surface` instead. Nvidia control panel / G-SYNC settings are still required to observe VRR.

## Build and run instructions

From the project root, with Ninja:

```
cmake -S . -B out/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/debug
./out/debug/vk-gsync-demo
```

On Windows, run that from a shell where the compiler and `VULKAN_SDK` are available (for MSVC, the x64 Native Tools prompt). The executable is `out\debug\vk-gsync-demo.exe`. CMake copies `SDL3.dll` next to it.

#### TODO
* OpenGL for GUI - same as in original project.
* VSync toggle

