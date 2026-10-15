# Compute workload prototype

Throwaway benchmark for [Set a measurable performance envelope for the 2D solver](https://github.com/prevosta/raylib-stam/issues/6).
It is not a fluid solver and must not be used as production code.

The benchmark runs a GPU-resident 2D `vec4` field through 20 pressure-like
five-point stencil passes and 4 bilinear backtrace/advection-like passes per
simulated frame. Each pass ping-pongs between SSBOs, with an OpenGL memory
barrier between dependent dispatches. It tests grid-size scaling, dispatch
overhead, and memory traffic without implementing the full numerical method,
display, or input path.

The CPU initializes each grid and uploads it once. Each measurement warms up
with 2 frames, then takes the median of 3 batches of 12 frames. Timing includes
dispatch submission and a final `glFinish`; approximate FPS is the reciprocal
of that average frame time. This proxy does not measure rendering, adapter
selection overhead, or a complete simulation.

## Run on Windows

Use MSYS2 UCRT64 GCC and CMake. The first configure fetches raylib 6.2.

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S prototypes/compute-workload -B build/compute-workload -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:\msys64\ucrt64\bin\gcc.exe" -DCMAKE_CXX_COMPILER="C:\msys64\ucrt64\bin\g++.exe" -DCMAKE_MAKE_PROGRAM="C:\msys64\ucrt64\bin\mingw32-make.exe" -DPROTOTYPE_REQUEST_NVIDIA=OFF
cmake --build build/compute-workload --config Release
Push-Location build\compute-workload
.\compute_workload_benchmark.exe
Pop-Location
```

Run again with `-DPROTOTYPE_REQUEST_NVIDIA=ON` in a separate build directory
to export NVIDIA's Optimus high-performance request:

```powershell
cmake -S prototypes/compute-workload -B build/compute-workload-nvidia -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:\msys64\ucrt64\bin\gcc.exe" -DCMAKE_CXX_COMPILER="C:\msys64\ucrt64\bin\g++.exe" -DCMAKE_MAKE_PROGRAM="C:\msys64\ucrt64\bin\mingw32-make.exe" -DPROTOTYPE_REQUEST_NVIDIA=ON
cmake --build build/compute-workload-nvidia --config Release
Push-Location build\compute-workload-nvidia
.\compute_workload_benchmark.exe
Pop-Location
```

Always verify the printed renderer. The NVIDIA export is a request, not a
guarantee; if the renderer remains Intel, record that adapter-selection method
as unsuccessful rather than attributing the result to the RTX A2000.

## Measurements on the development machine

Windows 11, raylib 6.2, OpenGL 4.3, MSYS2 UCRT64 GCC/G++ 16.2.0, MinGW Make
4.4.1. Each value is the median of 3 measured batches; frame rate is derived
from the compute workload's median frame time, not from a rendered app.

| Grid | Intel ms/frame | Intel approx. FPS | RTX A2000 ms/frame | RTX approx. FPS |
|---:|---:|---:|---:|---:|
| 128 x 128 | 0.768 | 1302.66 | 0.133 | 7529.18 |
| 256 x 256 | 0.890 | 1124.17 | 0.254 | 3929.92 |
| 384 x 384 | 1.184 | 844.51 | 0.782 | 1278.73 |
| 512 x 512 | 1.983 | 504.38 | 1.318 | 758.73 |
| 768 x 768 | 4.400 | 227.27 | 2.817 | 355.05 |
| 1024 x 1024 | 10.204 | 98.00 | 4.941 | 202.39 |
| 1536 x 1536 | 32.999 | 30.30 | 11.045 | 90.54 |
| 2048 x 2048 | 59.730 | 16.74 | 19.734 | 50.67 |

The default configuration selected Intel Iris Xe Graphics
(`32.0.101.7088`, OpenGL `4.3.0 - Build 32.0.101.7088`). The NVIDIA-request
configuration exported `NvOptimusEnablement` and selected the RTX A2000 Laptop
GPU (`596.52`, OpenGL `4.3.0 NVIDIA 596.52`).

Provisional choices for discussion:

- Conservative integrated target: at least 512 x 512 at 60 FPS on Intel Iris
  Xe. The proxy measured about 504 FPS at that grid, leaving headroom for work
  it does not model.
- Higher-resolution discrete target: at least 1024 x 1024 at 60 FPS on the
  RTX A2000. The proxy measured about 202 FPS there; validate the margin once
  the actual solver and rendering passes exist.

Neither recommendation is an accepted project requirement until the user
chooses it. The proxy excludes rendering, full velocity/dye field dependencies,
and other solver stages; its FPS must not be read as predicted application FPS.
