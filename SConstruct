#!/usr/bin/env python
import os
import sys

env = SConscript("godot-cpp/SConstruct")

sources = Glob("src/*.cpp")

env.Append(LIBS=[
    'libmpv',
    "GL"
    ])
env.Append(LIBPATH=['../dependencies/mpv-build/mpv/build'])  # Set library path
env.Append(CPPPATH=['../dependencies/mpv-build/mpv/include'])  # Set header path

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/bin/godot_mpv.{}.{}.framework/godot_mpv.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "demo/bin/godot_mpv.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "demo/bin/godot_mpv.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    # Ensure all necessary libraries are linked
    library = env.SharedLibrary(
        "demo/bin/godot_mpv{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )


Default(library)