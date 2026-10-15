#include "fluid_sim.h"

#include "rlgl.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLUID_SIM_GHOST_CELLS 1
#define FLUID_SIM_LOCAL_SIZE 16
#define FLUID_SIM_TIME_STEP (1.0f / 60.0f)
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000u
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200u
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020u
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008u
#define GL_FALSE 0
#define GL_WRITE_ONLY 0x88B9u
#define GL_RGBA8 0x8058u

typedef void (__stdcall *GlMemoryBarrierFunction)(unsigned int barriers);
typedef void (__stdcall *GlBindImageTextureFunction)(unsigned int unit, unsigned int texture, int level,
                                                      unsigned char layered, int layer, unsigned int access,
                                                      unsigned int format);

typedef void (__stdcall *GlGenericFunction)(void);
__declspec(dllimport) GlGenericFunction __stdcall wglGetProcAddress(const char *name);

typedef enum FluidProgramId
{
    PROGRAM_CLEAR_VELOCITY,
    PROGRAM_CLEAR_SCALAR,
    PROGRAM_COPY_VELOCITY,
    PROGRAM_SOURCE_VELOCITY,
    PROGRAM_SOURCE_DYE,
    PROGRAM_BOUNDARY_VELOCITY,
    PROGRAM_BOUNDARY_SCALAR,
    PROGRAM_DIFFUSE_VELOCITY,
    PROGRAM_DIVERGENCE,
    PROGRAM_PRESSURE_JACOBI,
    PROGRAM_SUBTRACT_GRADIENT,
    PROGRAM_ADVECT_VELOCITY,
    PROGRAM_ADVECT_DYE,
    PROGRAM_COLORIZE,
    PROGRAM_COUNT
} FluidProgramId;

typedef enum FluidUniformId
{
    UNIFORM_WIDTH,
    UNIFORM_HEIGHT,
    UNIFORM_TIME_STEP,
    UNIFORM_SOURCE_POSITION,
    UNIFORM_SOURCE_VELOCITY,
    UNIFORM_SOURCE_AMOUNT,
    UNIFORM_SOURCE_RADIUS,
    UNIFORM_ALPHA_X,
    UNIFORM_ALPHA_Y,
    UNIFORM_COUNT
} FluidUniformId;

typedef struct FluidProgram
{
    unsigned int id;
    int uniforms[UNIFORM_COUNT];
} FluidProgram;

struct FluidSim
{
    unsigned int width;
    unsigned int height;
    unsigned int allocatedWidth;
    unsigned int allocatedHeight;
    size_t cellCount;
    unsigned int solverIterations;
    float viscosity;
    unsigned int velocity[3];
    unsigned int dye[2];
    unsigned int pressure[2];
    unsigned int divergence;
    int velocityRead;
    int dyeRead;
    int pressureRead;
    Texture2D colorTexture;
    FluidProgram programs[PROGRAM_COUNT];
    GlMemoryBarrierFunction memoryBarrier;
    GlBindImageTextureFunction bindImageTexture;
};

static const char *const FLUID_UNIFORM_NAMES[UNIFORM_COUNT] = {
    "width", "height", "timeStep", "sourcePosition", "sourceVelocity", "sourceAmount",
    "sourceRadius", "alphaX", "alphaY"
};

static const char *const FLUID_SHADER_NAMES[PROGRAM_COUNT] = {
    "clear_velocity.comp",
    "clear_scalar.comp",
    "copy_velocity.comp",
    "source_velocity.comp",
    "source_dye.comp",
    "boundary_velocity.comp",
    "boundary_scalar.comp",
    "diffuse_velocity.comp",
    "divergence.comp",
    "pressure_jacobi.comp",
    "subtract_gradient.comp",
    "advect_velocity.comp",
    "advect_dye.comp",
    "colorize.comp"
};

static void SetError(char *message, size_t messageSize, const char *format, ...)
{
    if ((message == NULL) || (messageSize == 0))
    {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(message, messageSize, format, args);
    va_end(args);
}

static bool IsInvalidGlFunction(GlGenericFunction function)
{
    const uintptr_t address = (uintptr_t)function;
    return (address <= 3) || (address == UINTPTR_MAX);
}

static bool LoadOpenGlFunctions(FluidSim *simulation, char *errorMessage, size_t errorMessageSize)
{
    const GlGenericFunction memoryBarrier = wglGetProcAddress("glMemoryBarrier");
    const GlGenericFunction bindImageTexture = wglGetProcAddress("glBindImageTexture");
    if (IsInvalidGlFunction(memoryBarrier) || IsInvalidGlFunction(bindImageTexture))
    {
        SetError(errorMessage, errorMessageSize, "Could not load required OpenGL 4.3 compute functions");
        return false;
    }
    simulation->memoryBarrier = (GlMemoryBarrierFunction)memoryBarrier;
    simulation->bindImageTexture = (GlBindImageTextureFunction)bindImageTexture;
    return true;
}

static bool LoadProgram(FluidSim *simulation, FluidProgramId programId, const char *shaderDirectory,
                        char *errorMessage, size_t errorMessageSize)
{
    char shaderPath[1024];
    const int pathLength = snprintf(shaderPath, sizeof(shaderPath), "%s/%s", shaderDirectory,
                                    FLUID_SHADER_NAMES[programId]);
    if ((pathLength < 0) || ((size_t)pathLength >= sizeof(shaderPath)))
    {
        SetError(errorMessage, errorMessageSize, "Shader path is too long for %s",
                 FLUID_SHADER_NAMES[programId]);
        return false;
    }

    char *source = LoadFileText(shaderPath);
    if (source == NULL)
    {
        SetError(errorMessage, errorMessageSize, "Could not load compute shader: %s", shaderPath);
        return false;
    }

    const unsigned int shader = rlLoadShader(source, RL_COMPUTE_SHADER);
    UnloadFileText(source);
    if (shader == 0)
    {
        SetError(errorMessage, errorMessageSize, "Could not compile compute shader: %s", shaderPath);
        return false;
    }

    const unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0)
    {
        SetError(errorMessage, errorMessageSize, "Could not link compute shader program: %s", shaderPath);
        return false;
    }

    FluidProgram *fluidProgram = &simulation->programs[programId];
    fluidProgram->id = program;
    for (int uniform = 0; uniform < UNIFORM_COUNT; uniform++)
    {
        fluidProgram->uniforms[uniform] = rlGetLocationUniform(program, FLUID_UNIFORM_NAMES[uniform]);
    }
    return true;
}

static void SetIntUniform(const FluidProgram *program, FluidUniformId uniform, int value)
{
    if (program->uniforms[uniform] >= 0)
    {
        rlSetUniform(program->uniforms[uniform], &value, RL_SHADER_UNIFORM_INT, 1);
    }
}

static void SetFloatUniform(const FluidProgram *program, FluidUniformId uniform, float value)
{
    if (program->uniforms[uniform] >= 0)
    {
        rlSetUniform(program->uniforms[uniform], &value, RL_SHADER_UNIFORM_FLOAT, 1);
    }
}

static void SetVec2Uniform(const FluidProgram *program, FluidUniformId uniform, float x, float y)
{
    if (program->uniforms[uniform] >= 0)
    {
        const float value[2] = {x, y};
        rlSetUniform(program->uniforms[uniform], value, RL_SHADER_UNIFORM_VEC2, 1);
    }
}

static void SetGridUniforms(const FluidSim *simulation, const FluidProgram *program)
{
    SetIntUniform(program, UNIFORM_WIDTH, (int)simulation->width);
    SetIntUniform(program, UNIFORM_HEIGHT, (int)simulation->height);
}

static void DispatchGrid(const FluidSim *simulation, const FluidProgram *program)
{
    const unsigned int groupsX = (simulation->allocatedWidth + FLUID_SIM_LOCAL_SIZE - 1) /
                                 FLUID_SIM_LOCAL_SIZE;
    const unsigned int groupsY = (simulation->allocatedHeight + FLUID_SIM_LOCAL_SIZE - 1) /
                                 FLUID_SIM_LOCAL_SIZE;
    rlComputeShaderDispatch(groupsX, groupsY, 1);
}

static void StorageBarrier(const FluidSim *simulation)
{
    simulation->memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

static bool AllocateBuffer(unsigned int *buffer, size_t bytes)
{
    if (bytes > UINT_MAX)
    {
        return false;
    }
    *buffer = rlLoadShaderBuffer((unsigned int)bytes, NULL, RL_DYNAMIC_COPY);
    return *buffer != 0;
}

static bool AllocateResources(FluidSim *simulation, char *errorMessage, size_t errorMessageSize)
{
    const size_t velocityBytes = simulation->cellCount * 2 * sizeof(float);
    const size_t scalarBytes = simulation->cellCount * sizeof(float);

    for (int i = 0; i < 3; i++)
    {
        if (!AllocateBuffer(&simulation->velocity[i], velocityBytes))
        {
            SetError(errorMessage, errorMessageSize, "Could not allocate velocity field buffer %d", i);
            return false;
        }
    }
    for (int i = 0; i < 2; i++)
    {
        if (!AllocateBuffer(&simulation->dye[i], scalarBytes))
        {
            SetError(errorMessage, errorMessageSize, "Could not allocate dye field buffer %d", i);
            return false;
        }
        if (!AllocateBuffer(&simulation->pressure[i], scalarBytes))
        {
            SetError(errorMessage, errorMessageSize, "Could not allocate pressure field buffer %d", i);
            return false;
        }
    }
    if (!AllocateBuffer(&simulation->divergence, scalarBytes))
    {
        SetError(errorMessage, errorMessageSize, "Could not allocate divergence field buffer");
        return false;
    }

    simulation->colorTexture.id = rlLoadTexture(NULL, (int)simulation->width, (int)simulation->height,
                                                 RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    if (simulation->colorTexture.id == 0)
    {
        SetError(errorMessage, errorMessageSize, "Could not allocate dye color texture");
        return false;
    }
    simulation->colorTexture.width = (int)simulation->width;
    simulation->colorTexture.height = (int)simulation->height;
    simulation->colorTexture.mipmaps = 1;
    simulation->colorTexture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return true;
}

static void ClearVelocityBuffer(FluidSim *simulation, unsigned int buffer)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_CLEAR_VELOCITY];
    rlEnableShader(program->id);
    rlBindShaderBuffer(buffer, 0);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
}

static void ClearScalarBuffer(FluidSim *simulation, unsigned int buffer)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_CLEAR_SCALAR];
    rlEnableShader(program->id);
    rlBindShaderBuffer(buffer, 0);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
}

static void ApplyVelocityBoundary(FluidSim *simulation, unsigned int buffer)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_BOUNDARY_VELOCITY];
    rlEnableShader(program->id);
    rlBindShaderBuffer(buffer, 0);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
}

static void ApplyScalarBoundary(FluidSim *simulation, unsigned int buffer)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_BOUNDARY_SCALAR];
    rlEnableShader(program->id);
    rlBindShaderBuffer(buffer, 0);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
}

static void CopyVelocity(FluidSim *simulation, unsigned int source, unsigned int destination)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_COPY_VELOCITY];
    rlEnableShader(program->id);
    rlBindShaderBuffer(source, 0);
    rlBindShaderBuffer(destination, 1);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
}

static bool AddVelocitySource(FluidSim *simulation, float x, float y, float velocityX, float velocityY,
                              float radius)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_SOURCE_VELOCITY];
    rlEnableShader(program->id);
    rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 0);
    SetGridUniforms(simulation, program);
    SetVec2Uniform(program, UNIFORM_SOURCE_POSITION, x, y);
    SetVec2Uniform(program, UNIFORM_SOURCE_VELOCITY, velocityX, velocityY);
    SetFloatUniform(program, UNIFORM_SOURCE_RADIUS, radius);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
    ApplyVelocityBoundary(simulation, simulation->velocity[simulation->velocityRead]);
    return true;
}

static bool AddDyeSource(FluidSim *simulation, float x, float y, float amount, float radius)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_SOURCE_DYE];
    rlEnableShader(program->id);
    rlBindShaderBuffer(simulation->dye[simulation->dyeRead], 0);
    SetGridUniforms(simulation, program);
    SetVec2Uniform(program, UNIFORM_SOURCE_POSITION, x, y);
    SetFloatUniform(program, UNIFORM_SOURCE_AMOUNT, amount);
    SetFloatUniform(program, UNIFORM_SOURCE_RADIUS, radius);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
    ApplyScalarBoundary(simulation, simulation->dye[simulation->dyeRead]);
    return true;
}

static bool DiffuseVelocity(FluidSim *simulation)
{
    if (simulation->viscosity <= 0.0f)
    {
        return true;
    }

    const unsigned int original = simulation->velocity[2];
    CopyVelocity(simulation, simulation->velocity[simulation->velocityRead], original);

    const FluidProgram *program = &simulation->programs[PROGRAM_DIFFUSE_VELOCITY];
    const float alphaX = simulation->viscosity * FLUID_SIM_TIME_STEP *
                         (float)simulation->width * (float)simulation->width;
    const float alphaY = simulation->viscosity * FLUID_SIM_TIME_STEP *
                         (float)simulation->height * (float)simulation->height;
    for (unsigned int iteration = 0; iteration < simulation->solverIterations; iteration++)
    {
        const int write = 1 - simulation->velocityRead;
        rlEnableShader(program->id);
        rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 0);
        rlBindShaderBuffer(simulation->velocity[write], 1);
        rlBindShaderBuffer(original, 2);
        SetGridUniforms(simulation, program);
        SetFloatUniform(program, UNIFORM_TIME_STEP, FLUID_SIM_TIME_STEP);
        SetFloatUniform(program, UNIFORM_ALPHA_X, alphaX);
        SetFloatUniform(program, UNIFORM_ALPHA_Y, alphaY);
        DispatchGrid(simulation, program);
        rlDisableShader();
        StorageBarrier(simulation);
        ApplyVelocityBoundary(simulation, simulation->velocity[write]);
        simulation->velocityRead = write;
    }
    return true;
}

static bool ProjectVelocity(FluidSim *simulation)
{
    const FluidProgram *divergenceProgram = &simulation->programs[PROGRAM_DIVERGENCE];
    rlEnableShader(divergenceProgram->id);
    rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 0);
    rlBindShaderBuffer(simulation->divergence, 1);
    SetGridUniforms(simulation, divergenceProgram);
    DispatchGrid(simulation, divergenceProgram);
    rlDisableShader();
    StorageBarrier(simulation);

    ClearScalarBuffer(simulation, simulation->pressure[0]);
    ClearScalarBuffer(simulation, simulation->pressure[1]);
    simulation->pressureRead = 0;

    const FluidProgram *pressureProgram = &simulation->programs[PROGRAM_PRESSURE_JACOBI];
    for (unsigned int iteration = 0; iteration < simulation->solverIterations; iteration++)
    {
        const int write = 1 - simulation->pressureRead;
        rlEnableShader(pressureProgram->id);
        rlBindShaderBuffer(simulation->pressure[simulation->pressureRead], 0);
        rlBindShaderBuffer(simulation->pressure[write], 1);
        rlBindShaderBuffer(simulation->divergence, 2);
        SetGridUniforms(simulation, pressureProgram);
        DispatchGrid(simulation, pressureProgram);
        rlDisableShader();
        StorageBarrier(simulation);
        ApplyScalarBoundary(simulation, simulation->pressure[write]);
        simulation->pressureRead = write;
    }

    const int velocityWrite = 1 - simulation->velocityRead;
    const FluidProgram *gradientProgram = &simulation->programs[PROGRAM_SUBTRACT_GRADIENT];
    rlEnableShader(gradientProgram->id);
    rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 0);
    rlBindShaderBuffer(simulation->pressure[simulation->pressureRead], 1);
    rlBindShaderBuffer(simulation->velocity[velocityWrite], 2);
    SetGridUniforms(simulation, gradientProgram);
    DispatchGrid(simulation, gradientProgram);
    rlDisableShader();
    StorageBarrier(simulation);
    ApplyVelocityBoundary(simulation, simulation->velocity[velocityWrite]);
    simulation->velocityRead = velocityWrite;
    return true;
}

static bool AdvectVelocity(FluidSim *simulation)
{
    const int velocityWrite = 1 - simulation->velocityRead;
    const FluidProgram *program = &simulation->programs[PROGRAM_ADVECT_VELOCITY];
    rlEnableShader(program->id);
    rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 0);
    rlBindShaderBuffer(simulation->velocity[velocityWrite], 1);
    SetGridUniforms(simulation, program);
    SetFloatUniform(program, UNIFORM_TIME_STEP, FLUID_SIM_TIME_STEP);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
    ApplyVelocityBoundary(simulation, simulation->velocity[velocityWrite]);
    simulation->velocityRead = velocityWrite;
    return true;
}

static bool AdvectDye(FluidSim *simulation)
{
    const int dyeWrite = 1 - simulation->dyeRead;
    const FluidProgram *program = &simulation->programs[PROGRAM_ADVECT_DYE];
    rlEnableShader(program->id);
    rlBindShaderBuffer(simulation->dye[simulation->dyeRead], 0);
    rlBindShaderBuffer(simulation->dye[dyeWrite], 1);
    rlBindShaderBuffer(simulation->velocity[simulation->velocityRead], 2);
    SetGridUniforms(simulation, program);
    SetFloatUniform(program, UNIFORM_TIME_STEP, FLUID_SIM_TIME_STEP);
    DispatchGrid(simulation, program);
    rlDisableShader();
    StorageBarrier(simulation);
    ApplyScalarBoundary(simulation, simulation->dye[dyeWrite]);
    simulation->dyeRead = dyeWrite;
    return true;
}

static bool DiffuseDye(const FluidSim *simulation)
{
    (void)simulation;
    /* Keep dye diffusion in the step order; the agreed initial diffusivity is zero. */
    return true;
}

static bool Colorize(FluidSim *simulation)
{
    const FluidProgram *program = &simulation->programs[PROGRAM_COLORIZE];
    rlEnableShader(program->id);
    rlBindShaderBuffer(simulation->dye[simulation->dyeRead], 0);
    simulation->bindImageTexture(0, simulation->colorTexture.id, 0, GL_FALSE, 0,
                                 GL_WRITE_ONLY, GL_RGBA8);
    SetGridUniforms(simulation, program);
    DispatchGrid(simulation, program);
    rlDisableShader();
    simulation->memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                              GL_TEXTURE_FETCH_BARRIER_BIT);
    return true;
}

static unsigned int GetFieldBuffer(const FluidSim *simulation, FluidSimField field)
{
    switch (field)
    {
        case FLUID_SIM_FIELD_VELOCITY:
            return simulation->velocity[simulation->velocityRead];
        case FLUID_SIM_FIELD_DYE:
            return simulation->dye[simulation->dyeRead];
        case FLUID_SIM_FIELD_PRESSURE:
            return simulation->pressure[simulation->pressureRead];
        case FLUID_SIM_FIELD_DIVERGENCE:
            return simulation->divergence;
        default:
            return 0;
    }
}

FluidSim *FluidSimCreate(const FluidSimConfig *config, char *errorMessage, size_t errorMessageSize)
{
    if ((errorMessage != NULL) && (errorMessageSize > 0))
    {
        errorMessage[0] = '\0';
    }
    if ((config == NULL) || (config->width < 2) || (config->height < 2) ||
        (config->width > (unsigned int)(INT32_MAX - 2)) ||
        (config->height > (unsigned int)(INT32_MAX - 2)) ||
        (config->solverIterations == 0) || !isfinite(config->viscosity) ||
        (config->viscosity < 0.0f) || (config->shaderDirectory == NULL))
    {
        SetError(errorMessage, errorMessageSize, "Invalid fluid simulation configuration");
        return NULL;
    }
    if (rlGetVersion() != RL_OPENGL_43)
    {
        SetError(errorMessage, errorMessageSize, "The fluid simulation requires OpenGL 4.3");
        return NULL;
    }

    FluidSim *simulation = calloc(1, sizeof(*simulation));
    if (simulation == NULL)
    {
        SetError(errorMessage, errorMessageSize, "Could not allocate fluid simulation state");
        return NULL;
    }

    simulation->width = config->width;
    simulation->height = config->height;
    simulation->allocatedWidth = config->width + 2 * FLUID_SIM_GHOST_CELLS;
    simulation->allocatedHeight = config->height + 2 * FLUID_SIM_GHOST_CELLS;
    simulation->cellCount = (size_t)simulation->allocatedWidth * simulation->allocatedHeight;
    simulation->solverIterations = config->solverIterations;
    simulation->viscosity = config->viscosity;

    if ((simulation->cellCount > (size_t)INT_MAX) ||
        (simulation->cellCount > (size_t)UINT_MAX / (2 * sizeof(float))))
    {
        SetError(errorMessage, errorMessageSize, "Configured simulation grid is too large");
        FluidSimDestroy(simulation);
        return NULL;
    }
    if (!LoadOpenGlFunctions(simulation, errorMessage, errorMessageSize))
    {
        FluidSimDestroy(simulation);
        return NULL;
    }

    for (int program = 0; program < PROGRAM_COUNT; program++)
    {
        if (!LoadProgram(simulation, (FluidProgramId)program, config->shaderDirectory,
                         errorMessage, errorMessageSize))
        {
            FluidSimDestroy(simulation);
            return NULL;
        }
    }
    if (!AllocateResources(simulation, errorMessage, errorMessageSize) || !FluidSimClear(simulation))
    {
        if ((errorMessage != NULL) && (errorMessageSize > 0) && (errorMessage[0] == '\0'))
        {
            SetError(errorMessage, errorMessageSize, "Could not initialize fluid simulation resources");
        }
        FluidSimDestroy(simulation);
        return NULL;
    }
    return simulation;
}

void FluidSimDestroy(FluidSim *simulation)
{
    if (simulation == NULL)
    {
        return;
    }

    if (simulation->colorTexture.id != 0)
    {
        rlUnloadTexture(simulation->colorTexture.id);
    }
    for (int i = 0; i < 3; i++)
    {
        if (simulation->velocity[i] != 0)
        {
            rlUnloadShaderBuffer(simulation->velocity[i]);
        }
    }
    for (int i = 0; i < 2; i++)
    {
        if (simulation->dye[i] != 0)
        {
            rlUnloadShaderBuffer(simulation->dye[i]);
        }
        if (simulation->pressure[i] != 0)
        {
            rlUnloadShaderBuffer(simulation->pressure[i]);
        }
    }
    if (simulation->divergence != 0)
    {
        rlUnloadShaderBuffer(simulation->divergence);
    }
    for (int i = 0; i < PROGRAM_COUNT; i++)
    {
        if (simulation->programs[i].id != 0)
        {
            rlUnloadShaderProgram(simulation->programs[i].id);
        }
    }
    free(simulation);
}

bool FluidSimClear(FluidSim *simulation)
{
    if (simulation == NULL)
    {
        return false;
    }
    for (int i = 0; i < 3; i++)
    {
        ClearVelocityBuffer(simulation, simulation->velocity[i]);
    }
    for (int i = 0; i < 2; i++)
    {
        ClearScalarBuffer(simulation, simulation->dye[i]);
        ClearScalarBuffer(simulation, simulation->pressure[i]);
    }
    ClearScalarBuffer(simulation, simulation->divergence);
    simulation->velocityRead = 0;
    simulation->dyeRead = 0;
    simulation->pressureRead = 0;
    return Colorize(simulation);
}

bool FluidSimStep(FluidSim *simulation)
{
    if (simulation == NULL)
    {
        return false;
    }
    ApplyVelocityBoundary(simulation, simulation->velocity[simulation->velocityRead]);
    if (!DiffuseVelocity(simulation) || !ProjectVelocity(simulation) ||
        !AdvectVelocity(simulation) || !ProjectVelocity(simulation) || !DiffuseDye(simulation) ||
        !AdvectDye(simulation))
    {
        return false;
    }
    return Colorize(simulation);
}

bool FluidSimProjectVelocity(FluidSim *simulation)
{
    return (simulation != NULL) && ProjectVelocity(simulation);
}

bool FluidSimAddVelocitySource(FluidSim *simulation, float x, float y, float velocityX, float velocityY,
                               float radius)
{
    if ((simulation == NULL) || !isfinite(x) || !isfinite(y) || !isfinite(velocityX) ||
        !isfinite(velocityY) || !isfinite(radius) || (x < 0.0f) || (y < 0.0f) ||
        (x >= (float)simulation->width) || (y >= (float)simulation->height) || (radius <= 0.0f))
    {
        return false;
    }
    return AddVelocitySource(simulation, x, y, velocityX, velocityY, radius);
}

bool FluidSimAddDyeSource(FluidSim *simulation, float x, float y, float amount, float radius)
{
    if ((simulation == NULL) || !isfinite(x) || !isfinite(y) || !isfinite(amount) ||
        !isfinite(radius) || (x < 0.0f) || (y < 0.0f) || (x >= (float)simulation->width) ||
        (y >= (float)simulation->height) || (amount < 0.0f) || (radius <= 0.0f))
    {
        return false;
    }
    return AddDyeSource(simulation, x, y, amount, radius);
}

size_t FluidSimFieldElementCount(const FluidSim *simulation, FluidSimField field)
{
    if (simulation == NULL)
    {
        return 0;
    }
    if (field == FLUID_SIM_FIELD_VELOCITY)
    {
        return simulation->cellCount * 2;
    }
    if ((field == FLUID_SIM_FIELD_DYE) || (field == FLUID_SIM_FIELD_PRESSURE) ||
        (field == FLUID_SIM_FIELD_DIVERGENCE))
    {
        return simulation->cellCount;
    }
    return 0;
}

bool FluidSimReadField(FluidSim *simulation, FluidSimField field, float *values, size_t elementCount)
{
    if (simulation == NULL)
    {
        return false;
    }
    const size_t expectedCount = FluidSimFieldElementCount(simulation, field);
    const unsigned int buffer = GetFieldBuffer(simulation, field);
    if ((values == NULL) || (buffer == 0) || (expectedCount == 0) ||
        (elementCount != expectedCount) || (elementCount > (size_t)INT_MAX / sizeof(float)))
    {
        return false;
    }

    simulation->memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    rlReadShaderBuffer(buffer, values, (int)(elementCount * sizeof(float)), 0);
    return true;
}

Texture2D FluidSimGetTexture(const FluidSim *simulation)
{
    if (simulation == NULL)
    {
        return (Texture2D){0};
    }
    return simulation->colorTexture;
}
