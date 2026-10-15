# 2D Stable Fluids Architecture

Status: agreed architecture; the implementation follows this design. Full
1024 x 1024 performance acceptance still needs measurement on the RTX A2000.

## Goal and constraints

Build a simple, interactive 2D Stam Stable Fluids simulation in C with raylib on Windows. The simulation contains a velocity field and a scalar dye field. It targets a 1024 x 1024 active grid and at least 60 FPS on the NVIDIA RTX A2000 profile.

The compute-workload prototype is a dispatch and memory-traffic proxy, not a full-solver performance result. The full rendered solver must be measured on the target adapter before claiming the performance target.

Out of scope are 3D fluids, obstacles, physical-validation goals, guaranteed non-Windows support, and implementing the complete solver as part of this architecture decision.

## Numerical model

Use the conventional 2D Stam operator split:

1. Add user velocity sources.
2. Diffuse velocity using a small, tunable viscosity.
3. Project velocity to reduce divergence.
4. Advect velocity through the projected velocity field using semi-Lagrangian backtracing.
5. Project velocity again.
6. Add user dye sources.
7. Keep the dye-diffusion stage explicit in the model, but start with zero dye diffusivity so it is a no-op.
8. Advect dye through the updated velocity field.

Each projection computes divergence, solves the pressure Poisson equation with Jacobi iterations, and subtracts the pressure gradient. Start with 20 Jacobi sweeps per projection. Use the same tunable 20-sweep setting for the velocity-diffusion solve. These are initial budgets, not a claim of accuracy or frame-rate acceptance; tune them using measured divergence and full-solver frame time.

Advance the solver with a fixed 1/60-second timestep and one simulation update per rendered frame, with rendering capped at 60 FPS.

## Grid, fields, and boundaries

Use a cell-centered grid with 1024 x 1024 active cells and a one-cell ghost border on every side, for 1026 x 1026 allocated cells. Map mouse coordinates to active grid cells and keep sources inside that active region.

Store each field in its own float32 shader-storage buffer:

- Velocity as `vec2` per cell.
- Dye as one scalar per cell.
- Pressure as one scalar per cell.
- Divergence as one scalar per cell.

Use separate read and write buffers for Jacobi iterations and other operations that read and write the same field. Swap the buffers after each completed pass. Keep simulation fields GPU-resident during normal frames.

Use the closed-box Stam boundary convention. The normal velocity component is zero at the wall, the tangential velocity component is copied across it (free slip), and scalar fields such as pressure and dye use no-flux boundaries. Apply the corresponding ghost-cell values wherever a solver pass consumes the boundary.

## GPU execution and presentation

Use raylib 6.2 with the desktop OpenGL 4.3 backend and GLSL 4.30 compute shaders. Keep solver fields in SSBOs and use compute passes for the numerical stages. Give the dye visualization a final GPU colorization pass that writes a drawable color texture; do not read back complete fields to the CPU per frame.

Dispatches that exchange field data must be ordered with explicit OpenGL memory barriers appropriate to the next consumer. raylib's compute dispatch helper does not insert these barriers, and a workgroup barrier does not synchronize separate workgroups or dispatches. Use the appropriate shader-storage barrier between dependent SSBO passes and the appropriate image/texture visibility barrier before sampling the colorized output.

Render the scalar dye as one monochrome or single-tint ink quantity over a dark background. Keep the simulation square in a resizable window by preserving its aspect ratio and letterboxing when necessary.

At startup, request the high-performance adapter. Log the actual OpenGL renderer and warn if it is not the RTX A2000 target; the request is not a guarantee that a particular driver will select that adapter. Do not report the RTX performance target as met on another renderer.

## Interaction and controls

- Left-button dragging adds a localized velocity impulse based on pointer movement in grid coordinates. It does not add dye.
- Right-button dragging deposits dye. It does not change velocity.
- Keep brush radius and source strengths as named configuration values rather than adding sliders. Choose initial values during implementation and tune them by observing the interactive response.
- Provide only pause and clear/reset controls.

## Code organization

Keep the C layer small:

- `src/main.c` owns the raylib window, frame loop, input translation, and presentation.
- `src/fluid_sim.h` and `src/fluid_sim.c` expose and own the simulation lifecycle, GPU resources, shader setup, dispatch ordering, barriers, and field updates.
- Put the distinct compute stages in separate files under `shaders/fluid/`.
- Keep the existing compute smoke check under `tests/`; add small-grid GPU solver checks there rather than creating speculative C modules.
- Keep build targets and CTest registration in the root `CMakeLists.txt`. Keep this architecture document and the source-backed numerical reference under `docs/`.

## Correctness and performance checks

Do not build a CPU solver reference. Test the GPU implementation on a small deterministic grid with limited readback, covering:

- A zero state remains zero without sources.
- Velocity input changes the velocity field without depositing dye; dye input changes dye without changing velocity.
- Field values remain finite.
- Boundary handling enforces the selected wall behavior.
- Pressure projection reduces measured divergence.

Retain the compute-shader smoke test as a capability check. For performance acceptance, measure the complete rendered solver on the RTX A2000 at 1024 x 1024, including the actual solver passes, presentation, and normal frame loop. Record renderer and driver details. Tune iteration counts and source parameters as needed while keeping the acceptance bar explicit.

## Staged implementation plan

1. **Create the application shell and simulation seam.** Add `main.c` and the `fluid_sim` interface; create the raylib window, request and report the high-performance adapter, and preserve the existing smoke check.
2. **Create GPU state and boundary operations.** Allocate and clear the float32 SSBO fields for the 1024 x 1024 active grid plus ghost border. Implement field swapping, boundary handling, and the small-grid zero-state and wall checks.
3. **Add input sources and visualization.** Translate left and right drags into isolated GPU sources, produce the colorized dye texture, letterbox it in the window, and add pause and clear/reset controls.
4. **Implement advection and diffusion.** Add the velocity and dye advection passes, the tunable velocity-diffusion pass, and the explicit zero-dye-diffusion behavior. Add the required barriers and field swaps.
5. **Implement projection and integrate the full step.** Add divergence, the 20-sweep Jacobi pressure solve, gradient subtraction, and both projections in the agreed step order. Add the GPU invariant checks for source isolation, finite values, and divergence reduction.
6. **Measure and tune on the target profile.** Run the complete rendered solver at 1024 x 1024 on the RTX A2000, record the selected renderer and driver, and tune the iteration budget, viscosity, and brush constants against the correctness checks and the 60-FPS acceptance target.

For source-backed derivation of the solver stages, boundary alternatives, and OpenGL synchronization constraints, see [stable-fluids-reference.md](./stable-fluids-reference.md).
