# Godot MPV

## MPV video player for Godot Engine.

![Logo of GodotMPV](GodotMPV_Logo.png)

![Logo of Godot Engine](https://img.shields.io/badge/Godot%20Engine-478CBF?logo=godotengine&logoColor=white)
![Logo of mpv](https://img.shields.io/badge/MPV-691F69?logo=mpv&logoColor=white)
![Logo of linux](https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black)

This GDextension implements the open-source MPV video player in Godot Engine 4.4. It relies on Godot's compatibility rendering layer to share the OpenGL context between the two.

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

> Because of differences in C++ toolchains, this GDextension is only compatible with Godot on Linux at the moment, binary for windows will be soon available.

Everything you need to run this extension is located in <strong>demo/bin</strong>.

You can simply copy and paste the bin folder to your Godot project root directory, or just the files if folder already exist.

## Build from source

<strong>Requirements</strong>
- MPV installed on the system <em> (install step for [mpv-build](https://github.com/mpv-player/mpv-build) listed in the build steps)
- EGL and OpenGL ES2 <em>(usually comes with GPU drivers, if not present install with the command linked bellow this text)</em>
```bash
sudo apt install libegl1-mesa-dev libgles2-mesa-dev
```

### <em>build steps</em>
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