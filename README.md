# raylib-stam

A Windows-first 2D Stam Stable Fluids simulation written in C with raylib 6.2
and OpenGL 4.3 compute shaders. The application keeps the 1024 x 1024 velocity
and dye fields on the GPU and displays the scalar dye as tinted ink.

## Run

Requirements:

- Windows with an OpenGL 4.3-capable graphics driver
- CMake 3.22 or newer and Git
- MSYS2 UCRT64 GCC, G++, and `mingw32-make`
- Network access on the first configure so CMake can fetch raylib 6.2

From the repository root in PowerShell:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:\msys64\ucrt64\bin\gcc.exe" -DCMAKE_CXX_COMPILER="C:\msys64\ucrt64\bin\g++.exe" -DCMAKE_MAKE_PROGRAM="C:\msys64\ucrt64\bin\mingw32-make.exe"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
Set-Location build
.\raylib_stam.exe
```

Controls:

- **Left-button drag** stirs the flow without adding dye.
- **Right-button drag** deposits dye without changing velocity.
- **Space** pauses or resumes the simulation.
- **C** clears all fields.

The simulation remains square in a resizable window and is letterboxed when
necessary. At startup, it logs the selected OpenGL renderer and version, asks
Windows to prefer the high-performance adapter, and warns if the RTX A2000
target was not selected. The application shows its current FPS; the target is
at least 60 FPS for the complete rendered 1024 x 1024 solver on an RTX A2000.
It also logs five-second full-frame FPS averages. A reported renderer or a
60 FPS cap alone is not performance validation.
The solver advances once per rendered frame at a fixed 1/60-second step, and
pause freezes field evolution without stopping rendering.

## Checks

- `compute_shader_smoke` independently checks OpenGL 4.3 compute dispatch and
  deterministic shader-buffer readback.
- `fluid_sim_gpu` exercises the public simulation interface on a small
  deterministic GPU grid, checking zero-state stability, isolated sources,
  active-cell source bounds, finite fields, free-slip velocity and no-flux dye
  boundaries, pressure-projection divergence reduction, zero-diffusivity dye,
  dye advection, and clearing every simulation field.

The solver tests use the actual compute shaders with limited field readback;
they do not include a CPU solver reference. Full-resolution performance still
needs to be measured on the RTX A2000 using the rendered application.

## Project layout

- `src/main.c`: window, frame loop, input mapping, and presentation.
- `src/fluid_sim.h` and `src/fluid_sim.c`: public simulation seam and GPU
  resource/solver lifecycle.
- `shaders/fluid/`: separate GLSL compute stages for the solver and
  visualization.
- `tests/`: compute capability smoke check and small-grid GPU integration
  check.
- `docs/stable-fluids-architecture.md`: agreed numerical and GPU architecture.
- `docs/stable-fluids-reference.md`: source-backed method and OpenGL findings.
- `GLOSSARY.md`: project terminology.
