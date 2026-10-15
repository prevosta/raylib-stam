#ifndef FLUID_SIM_H
#define FLUID_SIM_H

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct FluidSim FluidSim;

typedef enum FluidSimField
{
    FLUID_SIM_FIELD_VELOCITY,
    FLUID_SIM_FIELD_DYE,
    FLUID_SIM_FIELD_PRESSURE,
    FLUID_SIM_FIELD_DIVERGENCE
} FluidSimField;

typedef struct FluidSimConfig
{
    unsigned int width;
    unsigned int height;
    unsigned int solverIterations;
    float viscosity;
    const char *shaderDirectory;
} FluidSimConfig;

FluidSim *FluidSimCreate(const FluidSimConfig *config, char *errorMessage, size_t errorMessageSize);
void FluidSimDestroy(FluidSim *simulation);
bool FluidSimClear(FluidSim *simulation);
bool FluidSimStep(FluidSim *simulation);
/* Projects the current velocity field without advancing the remaining simulation stages. */
bool FluidSimProjectVelocity(FluidSim *simulation);
/* Positions and radii use active-cell coordinates; velocity is measured in grid cells per second. */
bool FluidSimAddVelocitySource(FluidSim *simulation, float x, float y, float velocityX, float velocityY,
                               float radius);
bool FluidSimAddDyeSource(FluidSim *simulation, float x, float y, float amount, float radius);
size_t FluidSimFieldElementCount(const FluidSim *simulation, FluidSimField field);
/* Diagnostic readback is row-major and includes the one-cell ghost border. */
bool FluidSimReadField(FluidSim *simulation, FluidSimField field, float *values, size_t elementCount);
Texture2D FluidSimGetTexture(const FluidSim *simulation);

#endif
