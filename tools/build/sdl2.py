# PlatformIO pre-script for the desktop env: adds SDL2 compile and link flags.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

import os
import shutil
import subprocess


def sdl2_flags():
    cfg = shutil.which("sdl2-config")
    if cfg:
        out = subprocess.check_output([cfg, "--cflags", "--libs"]).decode()
        return out.split()
    for prefix in ("/opt/homebrew", "/usr/local"):
        if os.path.exists(os.path.join(prefix, "include", "SDL2", "SDL.h")):
            return [
                "-I" + os.path.join(prefix, "include", "SDL2"),
                "-L" + os.path.join(prefix, "lib"),
                "-lSDL2",
            ]
    raise SystemExit("SDL2 not found: run `brew install sdl2`")


env.MergeFlags(sdl2_flags())  # noqa: F821
