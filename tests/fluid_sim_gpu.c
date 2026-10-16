#include "fluid_sim.h"
#include "mouse_mapping.h"
#include "rlgl.h"
#include "simulation_tuning.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition, message) \
    do \
    { \
        if (!(condition)) \
        { \
            fputs(message "\n", stderr); \
            goto cleanup; \
        } \
    } while (0)

static bool AllZero(const float *values, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (values[i] != 0.0f)
        {
            return false;
        }
    }
    return true;
}

static bool QueueDyeTrailSource(Vector2 position, void *context)
{
    FluidSim *simulation = context;
    return FluidSimAddDyeSource(simulation, position, SIMULATION_DYE_SOURCE_AMOUNT,
                                SIMULATION_BRUSH_RADIUS);
}

static bool AllFinite(const float *values, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (!isfinite(values[i]))
        {
            return false;
        }
    }
    return true;
}

static bool AnyNonzero(const float *values, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (fabsf(values[i]) > 0.0001f)
        {
            return true;
        }
    }
    return false;
}

static double FieldDifference(const float *left, const float *right, size_t count)
{
    double difference = 0.0;
    for (size_t i = 0; i < count; i++)
    {
        difference += fabs((double)left[i] - right[i]);
    }
    return difference;
}

static double MeasureDivergence(const float *velocity, unsigned int width, unsigned int height)
{
    const unsigned int allocatedWidth = width + 2;
    double squaredDivergenceSum = 0.0;
    for (unsigned int y = 1; y <= height; y++)
    {
        for (unsigned int x = 1; x <= width; x++)
        {
            const size_t index = ((size_t)y*allocatedWidth + x)*2;
            const double divergence = 0.5*((double)velocity[index + 2] - velocity[index - 2] +
                                            (double)velocity[index + allocatedWidth*2 + 1] -
                                            velocity[index - allocatedWidth*2 + 1]);
            squaredDivergenceSum += divergence*divergence;
        }
    }
    return sqrt(squaredDivergenceSum/((double)width*height));
}

static bool HasClosedBoxVelocityBoundaries(const float *velocity, unsigned int width, unsigned int height)
{
    const unsigned int allocatedWidth = width + 2;
    for (unsigned int y = 1; y <= height; y++)
    {
        const size_t leftGhost = ((size_t)y*allocatedWidth)*2;
        const size_t leftCell = leftGhost + 2;
        const size_t rightCell = ((size_t)y*allocatedWidth + width)*2;
        const size_t rightGhost = rightCell + 2;
        if ((velocity[leftGhost] != -velocity[leftCell]) ||
            (velocity[leftGhost + 1] != velocity[leftCell + 1]) ||
            (velocity[rightGhost] != -velocity[rightCell]) ||
            (velocity[rightGhost + 1] != velocity[rightCell + 1]))
        {
            return false;
        }
    }

    for (unsigned int x = 1; x <= width; x++)
    {
        const size_t topGhost = (size_t)x*2;
        const size_t topCell = ((size_t)allocatedWidth + x)*2;
        const size_t bottomCell = ((size_t)(height*allocatedWidth) + x)*2;
        const size_t bottomGhost = ((size_t)((height + 1)*allocatedWidth) + x)*2;
        if ((velocity[topGhost] != velocity[topCell]) ||
            (velocity[topGhost + 1] != -velocity[topCell + 1]) ||
            (velocity[bottomGhost] != velocity[bottomCell]) ||
            (velocity[bottomGhost + 1] != -velocity[bottomCell + 1]))
        {
            return false;
        }
    }
    return true;
}

static bool HasNoFluxDyeBoundaries(const float *dye, unsigned int width, unsigned int height)
{
    const unsigned int allocatedWidth = width + 2;
    for (unsigned int y = 1; y <= height; y++)
    {
        const size_t leftGhost = (size_t)y*allocatedWidth;
        const size_t rightGhost = (size_t)y*allocatedWidth + width + 1;
        if ((dye[leftGhost] != dye[leftGhost + 1]) ||
            (dye[rightGhost] != dye[rightGhost - 1]))
        {
            return false;
        }
    }

    for (unsigned int x = 1; x <= width; x++)
    {
        const size_t topGhost = x;
        const size_t topCell = allocatedWidth + x;
        const size_t bottomCell = (size_t)height*allocatedWidth + x;
        const size_t bottomGhost = (size_t)(height + 1)*allocatedWidth + x;
        if ((dye[topGhost] != dye[topCell]) || (dye[bottomGhost] != dye[bottomCell]))
        {
            return false;
        }
    }
    return true;
}

int main(void)
{
    const unsigned int width = 32;
    const unsigned int height = 32;
    FluidSim *simulation = NULL;
    float *velocity = NULL;
    float *dye = NULL;
    float *pressure = NULL;
    float *divergence = NULL;
    int exitCode = 1;
    char errorMessage[256] = {0};

    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(16, 16, "Stable Fluids GPU integration test");
    if (!IsWindowReady())
    {
        return 1;
    }
    if (rlGetVersion() != RL_OPENGL_43)
    {
        fputs("The solver test requires raylib's OpenGL 4.3 backend.\n", stderr);
        goto cleanup;
    }

    const FluidSimConfig config = {
        .width = width,
        .height = height,
        .solverIterations = SIMULATION_SOLVER_ITERATIONS,
        .viscosity = SIMULATION_VISCOSITY,
        .shaderDirectory = "shaders/fluid"
    };
    simulation = FluidSimCreate(&config, errorMessage, sizeof(errorMessage));
    if (simulation == NULL)
    {
        fprintf(stderr, "Could not initialize the fluid simulation: %s\n",
                errorMessage[0] != '\0' ? errorMessage : "unspecified error");
        goto cleanup;
    }

    const size_t velocityCount = FluidSimFieldElementCount(simulation, FLUID_SIM_FIELD_VELOCITY);
    const size_t scalarCount = FluidSimFieldElementCount(simulation, FLUID_SIM_FIELD_DYE);
    velocity = calloc(velocityCount, sizeof(*velocity));
    dye = calloc(scalarCount, sizeof(*dye));
    pressure = calloc(scalarCount, sizeof(*pressure));
    divergence = calloc(scalarCount, sizeof(*divergence));
    CHECK((velocity != NULL) && (dye != NULL) && (pressure != NULL) && (divergence != NULL),
          "Could not allocate test readback buffers.");

    Texture2D colorTexture = FluidSimGetTexture(simulation);
    CHECK((colorTexture.id != 0) && (colorTexture.width == (int)width) &&
          (colorTexture.height == (int)height), "The GPU dye texture was not initialized correctly.");

    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not step and read the zero-state simulation.");
    CHECK(AllZero(velocity, velocityCount), "An unforced zero velocity field drifted.");
    CHECK(AllZero(dye, scalarCount), "An unforced zero dye field drifted.");
    puts("Zero-state GPU solver test passed.");

    CHECK(!FluidSimAddVelocitySource(simulation, (Vector2){-0.01f, 15.5f},
                                     (Vector2){24.0f, -9.0f}, 3.0f) &&
          !FluidSimAddVelocitySource(simulation, (Vector2){(float)width, 15.5f},
                                     (Vector2){24.0f, -9.0f}, 3.0f) &&
          !FluidSimAddDyeSource(simulation, (Vector2){15.5f, (float)height}, 2.0f, 3.0f) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "A source outside the active simulation grid was accepted.");
    CHECK(AllZero(velocity, velocityCount) && AllZero(dye, scalarCount),
          "An out-of-grid source changed a simulation field.");
    puts("Active-cell source bounds test passed.");

    MouseGridTrail dyeTrail = {0};
    CHECK(FluidSimClear(simulation) &&
          UpdateMouseGridTrail(&dyeTrail, true, (Vector2){2.0f, 16.0f}, width, height,
                               SIMULATION_BRUSH_RADIUS, QueueDyeTrailSource, simulation) &&
          UpdateMouseGridTrail(&dyeTrail, true, (Vector2){29.0f, 16.0f}, width, height,
                               SIMULATION_BRUSH_RADIUS, QueueDyeTrailSource, simulation) &&
          FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not apply and read a long dye trail.");
    const size_t middleDyeIndex = (size_t)17*(width + 2) + 17;
    CHECK(AllZero(velocity, velocityCount),
          "A dye-only trail changed the velocity field.");
    CHECK(dye[middleDyeIndex] > 0.1f,
          "A long dye trail did not deposit dye between its pointer samples.");
    puts("Continuous dye trail and dye-only interaction test passed.");

    CHECK(FluidSimClear(simulation) &&
          FluidSimAddVelocitySource(simulation, (Vector2){15.5f, 15.5f},
                                   (Vector2){SIMULATION_VELOCITY_SOURCE_GAIN*60.0f, -9.0f},
                                   SIMULATION_BRUSH_RADIUS) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not apply and read the velocity-only source.");
    CHECK(AnyNonzero(velocity, velocityCount), "A velocity source did not change the velocity field.");
    CHECK(AllZero(dye, scalarCount), "A velocity source unexpectedly changed the dye field.");
    const double divergenceBefore = MeasureDivergence(velocity, width, height);
    puts("Velocity source isolation test passed.");

    CHECK(FluidSimProjectVelocity(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_PRESSURE, pressure, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DIVERGENCE, divergence, scalarCount),
          "Could not project and read the sourced velocity field.");
    CHECK(AllFinite(velocity, velocityCount) && AllFinite(dye, scalarCount) &&
          AllFinite(pressure, scalarCount) && AllFinite(divergence, scalarCount),
          "A solver field contains a non-finite value.");
    CHECK(HasClosedBoxVelocityBoundaries(velocity, width, height),
          "The velocity field does not satisfy the closed-box wall convention.");
    const double divergenceAfter = MeasureDivergence(velocity, width, height);
    if (!(divergenceAfter < divergenceBefore))
    {
        fprintf(stderr, "Pressure projection did not reduce divergence (%.6f -> %.6f).\n",
                divergenceBefore, divergenceAfter);
        goto cleanup;
    }
    puts("Pressure-projection divergence-reduction test passed.");

    CHECK(FluidSimAddDyeSource(simulation, (Vector2){15.5f, 15.5f},
                               SIMULATION_DYE_SOURCE_AMOUNT, 3.0f) &&
          FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_PRESSURE, pressure, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DIVERGENCE, divergence, scalarCount),
          "Could not prepare nonzero values in every simulation field before clear.");
    CHECK(AnyNonzero(velocity, velocityCount) && AnyNonzero(dye, scalarCount) &&
          AnyNonzero(pressure, scalarCount) && AnyNonzero(divergence, scalarCount),
          "Could not populate every simulation field before clear.");
    CHECK(FluidSimClear(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_PRESSURE, pressure, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DIVERGENCE, divergence, scalarCount),
          "Could not clear all previously active simulation fields.");
    CHECK(AllZero(velocity, velocityCount) && AllZero(dye, scalarCount) &&
          AllZero(pressure, scalarCount) && AllZero(divergence, scalarCount),
          "Clear did not reset every simulation field.");
    puts("GPU clear-all-fields test passed.");

    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_PRESSURE, pressure, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DIVERGENCE, divergence, scalarCount),
          "Could not step and read the sourced simulation.");
    CHECK(AllFinite(velocity, velocityCount) && AllFinite(dye, scalarCount) &&
          AllFinite(pressure, scalarCount) && AllFinite(divergence, scalarCount),
          "A solver field contains a non-finite value.");
    CHECK(HasClosedBoxVelocityBoundaries(velocity, width, height),
          "The velocity field does not satisfy the closed-box wall convention.");
    puts("Finite-value and wall-boundary tests passed.");

    CHECK(FluidSimClear(simulation) &&
          FluidSimAddVelocitySource(simulation, (Vector2){15.5f, 15.5f},
                                   (Vector2){24.0f, 8.0f}, 6.0f) &&
          FluidSimStep(simulation) &&
          FluidSimAddDyeSource(simulation, (Vector2){17.5f, 15.5f},
                               SIMULATION_DYE_SOURCE_AMOUNT, 2.0f) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not prepare the deferred dye-source test.");
    CHECK(AllZero(dye, scalarCount),
          "A dye source was applied before the dye-advection stage.");
    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, pressure, scalarCount),
          "Could not advance and read the deferred dye-source test.");
    CHECK(AnyNonzero(pressure, scalarCount),
          "A queued dye source was not applied during the simulation step.");
    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not advance and read the dye-advection result.");
    CHECK(FieldDifference(pressure, dye, scalarCount) > 0.001,
          "The velocity field did not continue to carry dye.");
    puts("Dye advection test passed.");

    CHECK(FluidSimClear(simulation) &&
          FluidSimAddDyeSource(simulation, (Vector2){15.5f, 15.5f},
                               SIMULATION_DYE_SOURCE_AMOUNT, 3.0f) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not apply and read the dye-only source.");
    CHECK(AllZero(dye, scalarCount), "A queued dye source changed the field before the solver step.");
    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount),
          "Could not advance and read the dye-only source.");
    CHECK(AnyNonzero(dye, scalarCount), "A dye source did not change the dye field.");
    CHECK(AllZero(velocity, velocityCount), "A dye source unexpectedly changed the velocity field.");
    CHECK(HasNoFluxDyeBoundaries(dye, width, height),
          "The dye field does not satisfy the no-flux wall behavior.");
    puts("Dye source isolation test passed.");

    CHECK(FluidSimStep(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, pressure, scalarCount),
          "Could not advance and read the zero-diffusivity dye field.");
    CHECK(FieldDifference(dye, pressure, scalarCount) == 0.0,
          "Dye diffusion changed the field despite zero diffusivity.");
    puts("Zero-diffusivity dye stage test passed.");

    CHECK(FluidSimClear(simulation) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_VELOCITY, velocity, velocityCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DYE, dye, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_PRESSURE, pressure, scalarCount) &&
          FluidSimReadField(simulation, FLUID_SIM_FIELD_DIVERGENCE, divergence, scalarCount),
          "Could not clear and read the simulation fields.");
    CHECK(AllZero(velocity, velocityCount) && AllZero(dye, scalarCount) &&
          AllZero(pressure, scalarCount) && AllZero(divergence, scalarCount),
          "Clear did not reset every simulation field.");
    puts("GPU clear test passed.");
    exitCode = 0;

cleanup:
    free(velocity);
    free(dye);
    free(pressure);
    free(divergence);
    FluidSimDestroy(simulation);
    CloseWindow();
    return exitCode;
}
