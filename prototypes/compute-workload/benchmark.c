#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define GL_CALL __stdcall
#define GL_VENDOR_NAME 0x1F00u
#define GL_RENDERER_NAME 0x1F01u
#define GL_VERSION_NAME 0x1F02u
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000u
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200u

typedef void (GL_CALL *GlMemoryBarrierFunction)(unsigned int barriers);

__declspec(dllimport) const unsigned char *GL_CALL glGetString(unsigned int name);
__declspec(dllimport) GlMemoryBarrierFunction GL_CALL wglGetProcAddress(const char *name);
__declspec(dllimport) void GL_CALL glFinish(void);

#if defined(PROTOTYPE_REQUEST_NVIDIA)
__declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001u;
#endif
#else
#error This prototype currently uses the Windows WGL API.
#endif

typedef struct Float4
{
    float x;
    float y;
    float z;
    float w;
} Float4;

typedef struct WorkloadParameters
{
    int gridWidth;
    int mode;
} WorkloadParameters;

enum
{
    PRESSURE_PASSES = 20,
    ADVECTION_PASSES = 4,
    WARMUP_FRAMES = 2,
    SAMPLE_FRAMES = 12,
    SAMPLE_REPEATS = 3
};

static const unsigned int gridSizes[] = { 128, 256, 384, 512, 768, 1024, 1536, 2048 };

static double median3(double a, double b, double c)
{
    if (a > b) { double t = a; a = b; b = t; }
    if (b > c) { double t = b; b = c; c = t; }
    if (a > b) { double t = a; a = b; b = t; }
    return b;
}

static void dispatchPass(unsigned int inputBuffer, unsigned int outputBuffer,
                         unsigned int groups,
                         GlMemoryBarrierFunction memoryBarrier)
{
    rlBindShaderBuffer(inputBuffer, 0);
    rlBindShaderBuffer(outputBuffer, 1);
    rlComputeShaderDispatch(groups, groups, 1);
    memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

static void runFrame(unsigned int *inputBuffer, unsigned int *outputBuffer,
                     unsigned int groups,
                     unsigned int parametersBuffer, WorkloadParameters *parameters,
                     GlMemoryBarrierFunction memoryBarrier)
{
    parameters->mode = 0;
    rlUpdateShaderBuffer(parametersBuffer, parameters, sizeof(*parameters), 0);
    for (int pass = 0; pass < PRESSURE_PASSES; pass++)
    {
        dispatchPass(*inputBuffer, *outputBuffer, groups, memoryBarrier);
        unsigned int swap = *inputBuffer;
        *inputBuffer = *outputBuffer;
        *outputBuffer = swap;
    }

    parameters->mode = 1;
    rlUpdateShaderBuffer(parametersBuffer, parameters, sizeof(*parameters), 0);
    for (int pass = 0; pass < ADVECTION_PASSES; pass++)
    {
        dispatchPass(*inputBuffer, *outputBuffer, groups, memoryBarrier);
        unsigned int swap = *inputBuffer;
        *inputBuffer = *outputBuffer;
        *outputBuffer = swap;
    }
}

static int benchmarkGrid(unsigned int gridWidth, unsigned int program,
                         unsigned int parametersBuffer,
                         GlMemoryBarrierFunction memoryBarrier)
{
    uint64_t cellCount = (uint64_t)gridWidth*gridWidth;
    uint64_t byteCount64 = cellCount*sizeof(Float4);
    if (byteCount64 > UINT32_MAX)
    {
        fprintf(stderr, "Grid %u exceeds the raylib SSBO size limit.\n", gridWidth);
        return 0;
    }

    size_t byteCount = (size_t)byteCount64;
    Float4 *initialField = (Float4 *)malloc(byteCount);
    if (initialField == NULL)
    {
        fprintf(stderr, "Could not allocate the initial field for grid %u.\n", gridWidth);
        return 0;
    }

    for (uint64_t i = 0; i < cellCount; i++)
    {
        unsigned int x = (unsigned int)(i % gridWidth);
        unsigned int y = (unsigned int)(i / gridWidth);
        initialField[i].x = 0.2f + 0.1f*(float)x/(float)gridWidth;
        initialField[i].y = 0.1f + 0.1f*(float)y/(float)gridWidth;
        initialField[i].z = 0.5f;
        initialField[i].w = 0.25f;
    }

    unsigned int inputBuffer = rlLoadShaderBuffer((unsigned int)byteCount,
                                                  initialField, RL_DYNAMIC_COPY);
    unsigned int outputBuffer = rlLoadShaderBuffer((unsigned int)byteCount,
                                                   NULL, RL_DYNAMIC_COPY);
    free(initialField);
    if ((inputBuffer == 0) || (outputBuffer == 0))
    {
        fprintf(stderr, "Could not allocate the SSBOs for grid %u.\n", gridWidth);
        if (inputBuffer != 0) rlUnloadShaderBuffer(inputBuffer);
        if (outputBuffer != 0) rlUnloadShaderBuffer(outputBuffer);
        return 0;
    }

    WorkloadParameters parameters = { (int)gridWidth, 0 };
    rlUpdateShaderBuffer(parametersBuffer, &parameters, sizeof(parameters), 0);
    rlBindShaderBuffer(parametersBuffer, 2);

    unsigned int groups = (gridWidth + 15)/16;
    unsigned int warmupInput = inputBuffer;
    unsigned int warmupOutput = outputBuffer;
    rlEnableShader(program);
    for (int frame = 0; frame < WARMUP_FRAMES; frame++)
    {
        runFrame(&warmupInput, &warmupOutput, groups, parametersBuffer,
                 &parameters, memoryBarrier);
    }
    glFinish();

    double samples[SAMPLE_REPEATS];
    for (int repeat = 0; repeat < SAMPLE_REPEATS; repeat++)
    {
        glFinish();
        unsigned int sampleInput = warmupInput;
        unsigned int sampleOutput = warmupOutput;
        double startTime = GetTime();
        for (int frame = 0; frame < SAMPLE_FRAMES; frame++)
        {
            runFrame(&sampleInput, &sampleOutput, groups, parametersBuffer,
                     &parameters, memoryBarrier);
        }
        glFinish();
        samples[repeat] = 1000.0*(GetTime() - startTime)/(double)SAMPLE_FRAMES;
        warmupInput = sampleInput;
        warmupOutput = sampleOutput;
    }
    rlDisableShader();

    Float4 sampleValue = { 0 };
    memoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    rlReadShaderBuffer(warmupInput, &sampleValue, sizeof(sampleValue), 0);
    if (!isfinite(sampleValue.x))
    {
        fprintf(stderr, "The output for grid %u was not finite.\n", gridWidth);
        rlUnloadShaderBuffer(inputBuffer);
        rlUnloadShaderBuffer(outputBuffer);
        return 0;
    }

    double medianMs = median3(samples[0], samples[1], samples[2]);
    printf("%u,%d,%d,%.3f,%.2f\n", gridWidth,
           PRESSURE_PASSES + ADVECTION_PASSES, SAMPLE_FRAMES,
           medianMs, 1000.0/medianMs);

    rlUnloadShaderBuffer(inputBuffer);
    rlUnloadShaderBuffer(outputBuffer);
    return 1;
}

int main(void)
{
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(16, 16, "compute workload prototype");
    if (!IsWindowReady())
    {
        fputs("Could not create a raylib window and OpenGL context.\n", stderr);
        return 1;
    }

    if (rlGetVersion() != RL_OPENGL_43)
    {
        fprintf(stderr, "Expected the OpenGL 4.3 backend, got version %d.\n", rlGetVersion());
        CloseWindow();
        return 1;
    }

    const unsigned char *vendor = glGetString(GL_VENDOR_NAME);
    const unsigned char *renderer = glGetString(GL_RENDERER_NAME);
    const unsigned char *version = glGetString(GL_VERSION_NAME);
    printf("Vendor: %s\n", vendor != NULL ? (const char *)vendor : "unknown");
    printf("Renderer: %s\n", renderer != NULL ? (const char *)renderer : "unknown");
    printf("OpenGL: %s\n", version != NULL ? (const char *)version : "unknown");
#if defined(PROTOTYPE_REQUEST_NVIDIA)
    puts("Adapter request: NVIDIA high-performance Optimus export enabled");
#else
    puts("Adapter request: driver default");
#endif
    printf("Workload: %d pressure-like stencil passes + %d bilinear advection-like passes; "
           "%d warmup frames, %d x %d measured frames per grid.\n",
           PRESSURE_PASSES, ADVECTION_PASSES, WARMUP_FRAMES,
           SAMPLE_REPEATS, SAMPLE_FRAMES);

    char *shaderSource = LoadFileText("compute_workload.comp");
    if (shaderSource == NULL)
    {
        fputs("Could not load compute_workload.comp.\n", stderr);
        CloseWindow();
        return 1;
    }

    unsigned int computeShader = rlLoadShader(shaderSource, RL_COMPUTE_SHADER);
    UnloadFileText(shaderSource);
    if (computeShader == 0)
    {
        fputs("Could not compile the compute shader.\n", stderr);
        CloseWindow();
        return 1;
    }

    unsigned int program = rlLoadShaderProgramCompute(computeShader);
    rlUnloadShader(computeShader);
    if (program == 0)
    {
        fputs("Could not link the compute shader program.\n", stderr);
        CloseWindow();
        return 1;
    }

    GlMemoryBarrierFunction memoryBarrier = wglGetProcAddress("glMemoryBarrier");
    if (memoryBarrier == NULL)
    {
        fputs("Could not load glMemoryBarrier.\n", stderr);
        rlUnloadShaderProgram(program);
        CloseWindow();
        return 1;
    }

    WorkloadParameters parameters = { 1, 0 };
    unsigned int parametersBuffer = rlLoadShaderBuffer(sizeof(parameters),
                                                       &parameters, RL_DYNAMIC_COPY);
    if (parametersBuffer == 0)
    {
        fputs("Could not allocate the parameters SSBO.\n", stderr);
        rlUnloadShaderProgram(program);
        CloseWindow();
        return 1;
    }

    puts("grid,passes_per_frame,frames_per_sample,median_ms_per_frame,approx_fps");
    int success = 1;
    for (size_t i = 0; i < sizeof(gridSizes)/sizeof(gridSizes[0]); i++)
    {
        if (!benchmarkGrid(gridSizes[i], program, parametersBuffer, memoryBarrier))
        {
            success = 0;
            break;
        }
    }

    rlUnloadShaderBuffer(parametersBuffer);
    rlUnloadShaderProgram(program);
    CloseWindow();
    return success ? 0 : 1;
}
