This is Vulkan version of [gl-gsync-demo](https://github.com/dahenry/gl-gsync-demo) 

This demo can be used to verify if the VRR (GSync) is working. The it is using SDL to
create a fullscreen window. It's meant to run on X11 and not Wayland. 

The position of the rectangle is delivered to the vertex shader through push constant. The framerate 
changes autmatically in range from 30 to <max_refresh_rate>. 

Shaders are embedded into the binary. It was possible thanks to [bin2header](https://github.com/AntumDeluge/bin2header) 

The application was tested on Ubuntu 24.10 but it should also work on different Ubuntu versions
and/or distros.

Propertiary NVIDIA driver is probably also needed. I did not tested it with NVK.

## Dependencies
* Vulkan 1.0
* SDL2
* X11 dev libs
* Nvidia settings (for UI and GSYNC settings)

Ubuntu install dependencies with the following command:

```
sudo apt install libsdl2-dev libxnvctrl-dev libvulkan-dev
```

## Build and run instructions

In project's root directory, type:

``
make
``

And then tun

``
./vl-gsync-demo
``

#### TODO
* use VK_EXT_shader_object instead of graphic pipeline.
* OpenGL for GUI - same as in original project.
* VSync toggle

