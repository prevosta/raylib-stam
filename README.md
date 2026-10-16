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
The normal run logs five-second full-frame averages. To run a reproducible,
uncapped full-render benchmark, start it with `.\raylib_stam.exe --benchmark`
from the build directory. It warms up for five seconds, then measures ten
seconds while injecting fixed center velocity and dye sources. Each result
includes frame time, solver and brush settings, renderer, vendor, OpenGL
version/driver, and an acceptance status; only an RTX A2000 result at or above
60 FPS qualifies. The benchmark includes solver passes, colorization,
presentation, and the rendered frame loop.
The solver advances once per rendered frame at a fixed 1/60-second step, and
pause freezes field evolution without stopping rendering.
The selected architecture and full-solver measurements are recorded in
[ADR-0001](docs/adr/0001-gpu-stable-fluids.md).

## Checks

- `fluid_sim_gpu` exercises the public simulation interface and production
  compute shaders on a small deterministic GPU grid, checking zero-state
  stability, isolated sources, active-cell source bounds, finite fields,
  free-slip velocity and no-flux dye boundaries, pressure-projection
  divergence reduction, zero-diffusivity dye, dye advection, and clearing
  every simulation field. It also verifies the required compute capability.

The solver tests use the actual compute shaders with limited field readback;
they do not include a CPU solver reference. The GPU correctness checks use the
same tuned iteration and viscosity values as the application. Full-resolution
performance is measured separately with the rendered application.

GitHub Actions builds the project and runs CTest tests labeled `cpu`. The GPU
tests require an OpenGL 4.3 context and should be run on a GPU-capable machine.

## Commit messages

Commit subjects follow [the project format](docs/agents/commit-labels.md). To
enable the local commit-msg hook once per clone, run:

```powershell
git config core.hooksPath .githooks
```

GitHub Actions also validates commit subjects on pushes and pull requests.

## Project layout

- `src/main.c`: window, frame loop, input mapping, and presentation.
- `src/fluid_sim.h` and `src/fluid_sim.c`: public simulation seam and GPU
  resource/solver lifecycle.
- `src/simulation_tuning.h`: shared solver and interaction tuning values.
- `src/utils/`: shared mouse mapping and performance-report logic.
- `shaders/`: separate GLSL compute stages for the solver and visualization.
- `tests/`: small-grid GPU integration and CPU behavior checks.
- `docs/adr/`: accepted simulation architecture and performance decision.
- `GLOSSARY.md`: project terminology.
