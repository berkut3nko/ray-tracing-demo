# Vulkan Compute Ray Tracing Demo
A real-time ray tracer built from scratch using C++20 and Vulkan Compute Shaders. This project demonstrates efficient acceleration structure construction using Surface Area Heuristic (SAH) for BVH and interactive rendering.
## 🌟 Key Features

- `Vulkan Compute Pipeline:` Utilizing **compute shaders** for ray traversal and shading.

- `High-Performance BVH:` Custom implementation of Bounding Volume Hierarchy utilizing **Surface Area Heuristic (SAH)** for optimal ray intersection speeds.

- `Interactive UI:` Integrated Dear ImGui for **real-time control** over lighting, bounces, and model loading.

- `GLTF/GLB Support:` Robust model loading using **tinygltf**.

- `Async Loading:` Models are loaded in a separate thread to keep the UI responsive.

- `Camera System:` Switch between automated cinematic rotation and manual mouse control.

## 🛠 Technology Stack

- Language: `C++20 (Modules)`

- Graphics API: `Vulkan 1.2+`

- Windowing: `GLFW`

- GUI: `Dear ImGui`

- Asset Loading: `TinyGLTF`

- Math: `Custom vector math library`

- Testing: `GTest`

## 🚀 Quick Start

1. **Prerequisites (Linux/Debian-based)**

Ensure you have the Vulkan SDK, compiler, and necessary libraries installed:
```
sudo apt install cmake ninja-build clang-18 lld-18 libvulkan-dev vulkan-tools libwayland-dev libxkbcommon-dev xorg-dev glslc
```
`Note: Requires a GPU with Vulkan support.`
1. **Prerequisites (Windows)**

- CMake: Download and install from cmake.org. Add to system PATH.

- Ninja: Download binary from github.com/ninja-build/ninja/releases, extract ninja.exe, and add to system PATH.

- LLVM (Clang): Download and install LLVM-xx.x.x-win64.exe from github.com/llvm/llvm-project/releases. Add to system PATH.

- Vulkan SDK: Download and install from vulkan.lunarg.com. This includes headers, loader, validation layers, and glslc.

`Note: Libraries like libwayland-dev, libxkbcommon-dev, and xorg-dev are Linux-specific and not required on Windows as GLFW uses the Win32 API.`

2. **Fetch & Build**

Use the provided script to configure and build the project:
```
bash scripts/build_debug.sh
```
3. **Compile Shaders**

The compute shader must be compiled to SPIR-V before running:
```
glslc src/shaders/raytrace.comp -o src/shaders/raytrace.comp.spv
```

4. **Build & Run**
```
ninja -C build/debug && ./build/debug/RayTracingDemo
```

5. **Run**
```
./build/debug/RayTracingDemo
```
6. **Tests**
```
./build/debug/ctest
```


## 🎮 Controls
| Key / Input | Action |
| ----------- | ------ |
| Space | Toggle Camera Mode (Animation / Manual Mouse) |
| LMB + Drag | Orbit Camera (in Manual Mode) |
| UI Window | Change settings (Light pos, Bounces, Load Model) |

Tip: If the camera feels locked, ensure you are not hovering over the UI window.

## Gallery
### Frank
- [Sketchfab link](
https://sketchfab.com/3d-models/frank-0eb1f1757349489eab05a0f03cff5b46)

![frank.glb 3 rays](documentation/screenshots/Screenshot%20from%202026-01-17%2018-38-52.png)
![frank.glb 5 rays](documentation/screenshots/Screenshot%20from%202026-01-17%2018-39-17.png)
![frank.glb animation](documentation/gifs/2026-01-18%2010-04-05.gif)
### Erzengel
- [Sketchfab link](
https://sketchfab.com/3d-models/erzengel-82437f33573b4d9388302712f078984c)

![erzengel.glb](documentation/screenshots/Screenshot%20from%202026-01-18%2010-12-15.png)
![erzengel.glb](documentation/screenshots/Screenshot%20from%202026-01-18%2010-12-34.png)
![erzengel.glb animation](documentation/gifs/2026-01-18%2010-13-02.gif)
### Dragon sculpture 
- [Sketchfab link](https://sketchfab.com/3d-models/dragon-sculpture-6b997aff05bd47daa5cc392a45d9f43f)

![dragon_sculpture.glb](documentation/screenshots/Screenshot%20from%202026-01-18%2010-19-00.png)
![dragon_sculpture.glb](documentation/screenshots/Screenshot%20from%202026-01-18%2010-20-08.png)
![dragon_sculpture.glb animation](documentation/gifs/2026-01-18%2010-20-27.gif)

## 📦 Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui)
![logo](https://opengraph.githubassets.com/73b4f1ed343bd04d8ca28f1701fd66bd0795fd8b418ca4753f1ce937d38c40a6/ocornut/imgui_club)

- [TinyGLTF](https://github.com/syoyo/tinygltf)
![logo](https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcTccwoqb2jHLjpy0Cf1YGr6TgQTov782RGhfw&s)

- [GLFW](https://www.glfw.org/)
![logo](https://opengraph.githubassets.com/3a9bc4feb93d25215d62793166542cc640588420afba224923508130923711bd/glfw/glfw)


## 📞Contact
 - LinkedIn: [Oleksii Dizha](https://www.linkedin.com/in/oleksii-dizha-203905253/)

 - Telegram: [berkut3nko](https://t.me/berkut3nko)
 - GitHub: [berkut3nko](https://github.com/berkut3nko)