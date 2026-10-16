# Full-Solver Performance Validation

The compute-workload prototype is a GPU dispatch proxy and is not acceptance
evidence for the interactive simulation. This record measures the rendered
application at its full 1024 x 1024 solver resolution.

## Reproduce

Build the Release app and run the benchmark from the build directory:

```powershell
cmake --build build --config Release
Set-Location build
.\raylib_stam.exe --benchmark
```

Benchmark mode disables VSync and the frame-rate cap, runs the normal solver,
colorization, draw, and presentation loop, then exits after a five-second
warm-up and one ten-second sample. It injects fixed center velocity and dye
sources each frame and ignores interactive input. The report provides the
sample's actual rendered FPS and frame time, solver/brush parameters, OpenGL
renderer and vendor, OpenGL version/driver string, and target status.

The RTX A2000 acceptance status is reported only when the renderer identifies
an RTX A2000, the vendor identifies NVIDIA, and the measured full-frame average
is at least 60 FPS. Results from another adapter are not accepted.

## Measurements

Machine: NVIDIA RTX A2000 Laptop GPU, NVIDIA driver 596.52. The OpenGL renderer
was `NVIDIA RTX A2000 Laptop GPU/PCIe/SSE2`, vendor `NVIDIA Corporation`, and
OpenGL version/driver `4.3.0 NVIDIA 596.52`.

| Solver iterations | Viscosity | Brush radius | Velocity source gain | Dye source amount | Frames / seconds | Full-frame FPS | Frame time | Result |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 20 | 0.0001 | 8 | 0.45 | 2.0 | 1160 / 10.003 | 115.967 | 8.623 ms | Meets target |
| 32 | 0.0001 | 8 | 0.45 | 2.0 | 778 / 10.023 | 77.622 | 12.883 ms | Meets target |
| 32 (repeat) | 0.0001 | 8 | 0.45 | 2.0 | 777 / 10.003 | 77.673 | 12.874 ms | Meets target |

The selected configuration is 32 pressure/diffusion iterations. Increasing
the initial 20-sweep budget to 32 preserves the measured 60-FPS target while
providing more iterative solver work; the 32-iteration settings also passed
the small-grid GPU invariants. The measured configuration retains viscosity
0.0001, brush radius 8, velocity source gain 0.45, and dye source amount 2.0.
The GPU suite checks source isolation with the selected gain and dye amount at
the tuned solver settings; the fixed-source rendered benchmark also exercises
the configured brush radius and source strengths.
