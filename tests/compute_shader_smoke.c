#include "raylib.h"
#include "rlgl.h"

#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define GL_CALL __stdcall
#define GL_RENDERER_NAME 0x1F01u
#define GL_VERSION_NAME 0x1F02u
#define GL_BUFFER_UPDATE_BARRIER 0x00000200u

typedef const unsigned char *(GL_CALL *GlGetStringFunction)(unsigned int name);
typedef void (GL_CALL *GlMemoryBarrierFunction)(unsigned int barriers);

__declspec(dllimport) const unsigned char *GL_CALL glGetString(unsigned int name);
__declspec(dllimport) GlMemoryBarrierFunction GL_CALL wglGetProcAddress(const char *name);
#else
#error This smoke test currently loads OpenGL functions through Windows WGL.
#endif

int main(void)
{
    const uint32_t expected = 0x5A17u;
    uint32_t result = 0;
    int exitCode = 1;
    char *shaderSource = NULL;
    unsigned int computeShader = 0;
    unsigned int computeProgram = 0;
    unsigned int resultBuffer = 0;

    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(16, 16, "raylib compute shader smoke test");
    if (!IsWindowReady())
    {
        fputs("Could not create a raylib window and OpenGL context.\n", stderr);
        return 1;
    }

    if (rlGetVersion() != RL_OPENGL_43)
    {
        fprintf(stderr, "Expected the OpenGL 4.3 backend, got version %d.\n", rlGetVersion());
        goto cleanup;
    }

    const unsigned char *renderer = glGetString(GL_RENDERER_NAME);
    const unsigned char *version = glGetString(GL_VERSION_NAME);
    printf("OpenGL renderer: %s\n", renderer != NULL ? (const char *)renderer : "unknown");
    printf("OpenGL version: %s\n", version != NULL ? (const char *)version : "unknown");

    shaderSource = LoadFileText("compute_smoke.comp");
    if (shaderSource == NULL)
    {
        fputs("Could not load compute_smoke.comp.\n", stderr);
        goto cleanup;
    }

    computeShader = rlLoadShader(shaderSource, RL_COMPUTE_SHADER);
    UnloadFileText(shaderSource);
    shaderSource = NULL;
    if (computeShader == 0)
    {
        fputs("Could not compile the compute shader.\n", stderr);
        goto cleanup;
    }

    computeProgram = rlLoadShaderProgramCompute(computeShader);
    if (computeProgram == 0)
    {
        fputs("Could not link the compute shader program.\n", stderr);
        goto cleanup;
    }

    resultBuffer = rlLoadShaderBuffer(sizeof(result), &result, RL_DYNAMIC_COPY);
    if (resultBuffer == 0)
    {
        fputs("Could not allocate the shader storage buffer.\n", stderr);
        goto cleanup;
    }

    GlMemoryBarrierFunction memoryBarrier = wglGetProcAddress("glMemoryBarrier");
    if (memoryBarrier == NULL)
    {
        fputs("Could not load glMemoryBarrier.\n", stderr);
        goto cleanup;
    }

    rlEnableShader(computeProgram);
    rlBindShaderBuffer(resultBuffer, 0);
    rlComputeShaderDispatch(1, 1, 1);
    rlDisableShader();

    memoryBarrier(GL_BUFFER_UPDATE_BARRIER);
    rlReadShaderBuffer(resultBuffer, &result, sizeof(result), 0);
    if (result != expected)
    {
        fprintf(stderr, "Unexpected compute result: 0x%04X (expected 0x%04X).\n",
                result, expected);
        goto cleanup;
    }

    printf("Compute shader passed: result=0x%04X\n", result);
    exitCode = 0;

cleanup:
    if (shaderSource != NULL) UnloadFileText(shaderSource);
    if (resultBuffer != 0) rlUnloadShaderBuffer(resultBuffer);
    if (computeProgram != 0) rlUnloadShaderProgram(computeProgram);
    if (computeShader != 0) rlUnloadShader(computeShader);
    CloseWindow();
    return exitCode;
}
