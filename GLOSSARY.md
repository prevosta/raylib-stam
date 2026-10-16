# Raylib Stam

This context explores a 2D interactive fluid simulation using raylib. Its domain language describes the Stable Fluids model and the fields used to represent and visualize it.

## Fluid simulation

**Stable Fluids**:
A grid-based, incompressible fluid model based on Jos Stam's method. This project targets its 2D interactive form.

**Velocity field**:
A vector field describing the fluid's motion across the simulation grid.

**Dye field**:
A scalar tracer carried by the velocity field and visualized as ink or smoke; it represents visible dye, not physical mass density.
_Avoid_: density field, when referring to the visual tracer

**Semi-Lagrangian advection**:
A transport method that traces backward through the velocity field to sample the value carried into each cell.

**Pressure projection**:
A correction that reduces velocity-field divergence by solving for pressure and subtracting its gradient.

**Closed-box boundary**:
A fluid-domain edge that blocks flow through the wall while allowing tangential flow; scalar fields do not flow through it.
