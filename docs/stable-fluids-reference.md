# Stable Fluids: Concepts, Findings, and Sources

This page summarizes evidence for the project's 2D velocity-and-dye scope. It
supports [Review Stam Stable Fluids and GPU-compute implementation constraints](https://github.com/prevosta/raylib-stam/issues/4);
it is a research note, not a selected architecture or a solver specification.
Terms follow the [project glossary](../GLOSSARY.md).

## Concepts

- **Velocity field**: a vector at each grid cell describing fluid motion.
- **Dye field**: a scalar tracer carried by the velocity field and visualized
  as ink or smoke. Stam's papers call this scalar "density"; this project uses
  "dye" to avoid implying physical mass density.
- **Semi-Lagrangian advection**: for each destination cell, trace backward
  through the velocity field to find where its value came from, then
  interpolate there. Stam uses this to obtain a stable advection step for
  large time steps, at the cost of numerical dissipation.
- **Pressure projection**: compute velocity divergence, solve a Poisson
  equation for a scalar pressure, then subtract its gradient from velocity.
  This makes the velocity approximately divergence-free; a finite iterative
  solve leaves a residual.
- **Diffusion**: an implicit solve that spreads velocity or dye to neighboring
  cells. Stam's sample uses Gauss-Seidel relaxation; the GPU Gems
  implementation uses Jacobi iterations for its Poisson solves.

## Findings from the sources

Stam's 2003 game-oriented implementation uses a cell-centered 2D grid with a
ghost-cell border. A velocity step adds forces, diffuses velocity, projects,
advects velocity, then projects again. A dye step adds sources, diffuses dye,
then advects it by velocity. Its sample pressure solve uses 20 Gauss-Seidel
iterations; this is a reference implementation detail, not a performance
target for this project. [2]

Boundary behavior must be chosen explicitly. Stam's closed-box example
prevents velocity from passing through the walls and assumes continuity for
scalar fields. GPU Gems describes a no-slip velocity boundary. These are
different boundary assumptions; choose based on the intended interaction and
visual behavior rather than treating the boundary code as incidental. [2][3]

GPU Gems presents a closely related 2D stable-fluid method on textures using
the graphics pipeline, not OpenGL compute shaders. Its data-parallel passes
and iterative Jacobi solve are useful precedents, but its rendering API and
synchronization code are not directly reusable as compute-shader code. [3]

## Mapping to raylib and OpenGL compute

- raylib 6.2's official compute example requires its OpenGL 4.3 backend and
  GLSL 4.30 shaders. It demonstrates `rlLoadShaderProgramCompute`,
  `rlComputeShaderDispatch`, and shader storage buffers. [4]
- In raylib 6.2, `rlComputeShaderDispatch` calls `glDispatchCompute`;
  `rlReadShaderBuffer` reads with `glGetBufferSubData`. The helpers do not add
  a memory barrier. Select an OpenGL memory-barrier bit for the next consumer
  (for example, another shader-storage access or a buffer readback); a
  workgroup barrier is not a device-wide synchronization point. [5][6][7]
- A straightforward Jacobi mapping is one pass per iteration: read the
  previous pressure field, write a distinct next field, apply the required
  inter-dispatch visibility operation, and swap. Keep simulation fields on the
  GPU and send only small interaction inputs from the CPU where possible;
  frequent full-field readbacks add synchronization and transfer cost. This is
  a recommendation, not a measured result.
- A float32 lower-bound storage estimate for velocity plus dye is
  `3 * 4 * N * N` bytes for one copy: 3 MiB at 512 x 512, or 12 MiB at
  1024 x 1024. Ping-pong doubles that amount; pressure, divergence, temporary
  fields, and render targets add more. These are storage estimates, not
  throughput measurements.

The scaffold verified shader dispatch and readback on the Intel Iris Xe at
OpenGL 4.3 (`0x5A17`); raylib selected the integrated adapter by default even
though an RTX A2000 is also present. This does not establish fluid-solver
performance or RTX behavior. See the [compute smoke test](../tests/compute_shader_smoke.c) and
[performance-envelope ticket](https://github.com/prevosta/raylib-stam/issues/6).

## Questions still open

- Which wall behavior should the interactive simulation use?
- Which grid size, frame-rate floor, and adapter define the performance target?
- What diffusion/viscosity values and iterative-solver budget provide the
  desired visual behavior within that target?
- Should the implementation use image textures or shader storage buffers, and
  which precision is sufficient?
- Would a small CPU reference materially improve correctness checks?

These questions should be settled through the map's performance, repository
structure, and architecture tickets rather than inferred from the smoke test.

## Sources

1. Jos Stam, ["Stable Fluids"](https://www.dgp.toronto.edu/people/stam/reality/Research/pdf/ns.pdf),
   SIGGRAPH 1999. Author-hosted paper.
2. Jos Stam, ["Real-Time Fluid Dynamics for Games"](https://www.dgp.toronto.edu/people/stam/reality/Research/pdf/GDC03.pdf),
   GDC 2003. Author-hosted paper and complete C implementation.
3. Mark J. Harris, ["Fast Fluid Dynamics Simulation on the GPU"](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu),
   GPU Gems, Chapter 38. NVIDIA-hosted chapter.
4. raylib, [compute-shader example at tag 6.2](https://github.com/raysan5/raylib/blob/6.2/examples/shaders/shaders_rlgl_compute.c).
5. raylib, [rlgl compute and buffer APIs at tag 6.2](https://github.com/raysan5/raylib/blob/6.2/src/rlgl.h).
6. Khronos Group, [OpenGL 4.3 Core Profile Specification](https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf).
7. Khronos Group, [glDispatchCompute](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glDispatchCompute.xhtml)
   and [glMemoryBarrier](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMemoryBarrier.xhtml)
   reference pages.
