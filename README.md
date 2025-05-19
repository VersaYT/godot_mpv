# Godot MPV

## MPV video player for Godot Engine.

![Logo of GodotMPV](GodotMPV_Logo.png)

![Logo of Godot Engine](https://img.shields.io/badge/Godot%20Engine-478CBF?logo=godotengine&logoColor=white)
![Logo of mpv](https://img.shields.io/badge/MPV-691F69?logo=mpv&logoColor=white)
![Logo of linux](https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black)

This GDextension implements the open-source MPV video player in Godot Engine 4.4. It's capable of playing local and http video stream to your MeshInstance2D or MeshInstance3D plane surfaces. It relies on Godot's compatibility rendering layer wich uses OpenGL.

---
[Usage](#usage)
[Installation](#installation)
[Build from source](#build-from-source)

## Usage

```gdscript
extends Node3D

var mpv_player: MPVPlayer

func _ready():
    mpv_player = MPVPlayer.new()
    add_child(mpv_player)
    mpv_player.initialize()

    #Store reference to a MeshInstance3D (plane)
    var mesh_instance = $Screen

    mpv_player.connect("texture_updated", func(texture):
        var material = mesh_instance.get_surface_override_material(0)
        if not material:
            material = StandardMaterial3D.new()
            mesh_instance.set_surface_override_material(0, material)
        material.albedo_texture = texture
        material.shading_mode = StandardMaterial3D.SHADING_MODE_UNSHADED
    )

    mpv_player.load_file("res://path/to/your_video.mp4")
    mpv_player.play()
```

## Installation

Download and extract the GDextension files from the release page into your project ```bin``` directory.

## Build from source


## Linux
<strong>Requirements</strong>
- MPV installed on the system <em> (install step for [mpv-build](https://github.com/mpv-player/mpv-build) listed in the build steps)</em>
- EGL and OpenGL ES2 <em>(usually comes with GPU drivers, if not present install with the command linked bellow this text)</em>
```bash
sudo apt install libegl1-mesa-dev libgles2-mesa-dev
```
- CMake for project compilation <em>([CMake download page](https://cmake.org/download/))</em>

### <em>build steps</em>
Install CMake (link above)


Clone the project
```bash
git clone git@github.com:VersaYT/godot_mpv.git
```
Cd into dependencies folder and clone mpv-build
```bash
cd godot_mpv && cd dependencies
git clone https://github.com/mpv-player/mpv-build.git
cd mpv-build
```
Fetch mpv packages
```bash
./rebuild -j4
```
The ```-j4``` asks it to use 4 parallel processes.

Install mpv
```bash
sudo ./install
```
cd at the root directory of the project
Configure CMake for the project
```bash
cmake . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
```
Compile the project (you can of course allow more threads when compiling "-j8")
```bash
cmake --build . -- -j4
```
---
## Windows
<strong>Requirements</strong>
- GCC compiler for windows (can be downloaded [here](https://winlibs.com))
- A compiled version of libmpv <em> (you can find one here [sourceforge](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/))</em>
- ANGLE (for EGL and GLES2 headers) <em>(will be installed in the project when you use ```CMake .``` command)</em>
- vcpkg (to retrieve ANGLE library)
- GLAD (to load OpenGL and use EGL/GLES2 headers, use the generator [here](https://gen.glad.sh/))
> GLAD configuration: 
> EGL 1.5 
> GLES2 3.2
> ✅ Loader
- CMake for project compilation <em>([CMake download page](https://cmake.org/download/))</em>

### <em>build steps</em>
GCC compiler setup (if you don't have it)
- Download and extract the archive found in the website linked above (any version > GCC 12)
- Type "Env" in windows search and click on "edit system environment variables"
- Click on "environment variables"
- Look in "system variables" for your "Path" variable
- Add a new line to this variable
- Copy and paste the path to the previously extracted gcc compiler (we want to target the bin/ folder) for example ```C:\mingw64\bin```

Install CMake (link above)

Clone the project
```powershell
git clone git@github.com:VersaYT/godot_mpv.git
```
Pull vcpkg submodule
```powershell
cd godot_mpv
git submodule update --init
```
cd into dependencies folder
```powershell
cd dependencies
```
Download libmpv and GLAD from the links above
- Create an ```mpv-dev``` folder and extract the content of downloaded mpv into it
- Create a ```glad``` folder and extract content of downloaded GLAD into it


From powershell, make sure CMake and GCC are working
```powershell
cmake --version
gcc --version
```
cd at the root directory of the project
Configure CMake for the project
```powershell
cmake . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
```
Compile the project (you can of course allow more threads when compiling "-j8")
```powershell
mingw32-make -j4
```
