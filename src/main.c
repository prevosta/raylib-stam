#include "fluid_sim.h"
#include "mouse_mapping.h"
#include "rlgl.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 0x00000001;
#define GL_RENDERER_NAME 0x1F01u
#define GL_VERSION_NAME 0x1F02u
#define GL_VENDOR_NAME 0x1F00u

__declspec(dllimport) const unsigned char *__stdcall glGetString(unsigned int name);
#else
#error This application currently targets Windows desktop OpenGL.
#endif

enum
{
    SIMULATION_SIZE = 1024,
    SOLVER_ITERATIONS = 20,
    TARGET_FRAME_RATE = 60
};

static const float VELOCITY_VISCOSITY = 0.0001f;
static const float BRUSH_RADIUS = 8.0f;
static const float VELOCITY_SOURCE_GAIN = 0.45f;
static const float DYE_SOURCE_AMOUNT = 2.0f;
static const Color BACKGROUND_COLOR = {5, 7, 9, 255};

static bool GetSimulationRectangle(int screenWidth, int screenHeight, Rectangle *rectangle)
{
    const float side = (float)((screenWidth < screenHeight) ? screenWidth : screenHeight);
    if ((side <= 0.0f) || (rectangle == NULL))
    {
        return false;
    }
    rectangle->x = ((float)screenWidth - side)*0.5f;
    rectangle->y = ((float)screenHeight - side)*0.5f;
    rectangle->width = side;
    rectangle->height = side;
    return true;
}

int main(void)
{
    FluidSim *simulation = NULL;
    int exitCode = 1;
    bool paused = false;
    bool hasPreviousMouse = false;
    Vector2 previousMouse = {0};
    double measuredSeconds = 0.0;
    unsigned int measuredFrames = 0;

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1200, 900, "raylib-stam | 2D Stable Fluids");
    if (!IsWindowReady())
    {
        fputs("Could not create a raylib window and OpenGL context.\n", stderr);
        return 1;
    }

    if (rlGetVersion() != RL_OPENGL_43)
    {
        TraceLog(LOG_ERROR, "The simulation requires raylib's OpenGL 4.3 backend.");
        goto cleanup;
    }

    const unsigned char *rendererBytes = glGetString(GL_RENDERER_NAME);
    const unsigned char *versionBytes = glGetString(GL_VERSION_NAME);
    const unsigned char *vendorBytes = glGetString(GL_VENDOR_NAME);
    const char *renderer = (rendererBytes != NULL) ? (const char *)rendererBytes : "unknown";
    TraceLog(LOG_INFO, "OpenGL renderer: %s", renderer);
    TraceLog(LOG_INFO, "OpenGL vendor: %s",
             (vendorBytes != NULL) ? (const char *)vendorBytes : "unknown");
    TraceLog(LOG_INFO, "OpenGL version: %s",
             (versionBytes != NULL) ? (const char *)versionBytes : "unknown");
    if (strstr(renderer, "RTX A2000") == NULL)
    {
        TraceLog(LOG_WARNING,
                 "The RTX A2000 target was not selected. The 1024x1024, 60 FPS full-solver target "
                 "has not been verified on this adapter.");
    }
    else
    {
        TraceLog(LOG_INFO,
                 "RTX A2000 selected; verify a full rendered 1024x1024 run before claiming 60 FPS.");
    }

    const FluidSimConfig config = {
        .width = SIMULATION_SIZE,
        .height = SIMULATION_SIZE,
        .solverIterations = SOLVER_ITERATIONS,
        .viscosity = VELOCITY_VISCOSITY,
        .shaderDirectory = "shaders/fluid"
    };
    char errorMessage[512] = {0};
    simulation = FluidSimCreate(&config, errorMessage, sizeof(errorMessage));
    if (simulation == NULL)
    {
        TraceLog(LOG_ERROR, "Could not initialize the fluid simulation: %s",
                 errorMessage[0] != '\0' ? errorMessage : "unspecified error");
        goto cleanup;
    }

    SetTargetFPS(TARGET_FRAME_RATE);
    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_SPACE))
        {
            paused = !paused;
            hasPreviousMouse = false;
        }
        if (IsKeyPressed(KEY_C) && !FluidSimClear(simulation))
        {
            TraceLog(LOG_ERROR, "Could not clear the simulation fields.");
            goto cleanup;
        }

        Rectangle simulationRectangle = {0};
        Vector2 mouse = GetMousePosition();
        Vector2 gridPosition = {0};
        const bool mouseInside = GetSimulationRectangle(GetScreenWidth(), GetScreenHeight(),
                                                         &simulationRectangle) &&
                                 MapMouseToGrid(mouse, simulationRectangle, (float)SIMULATION_SIZE,
                                                (float)SIMULATION_SIZE, &gridPosition);
        if (!paused && mouseInside)
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                if (hasPreviousMouse)
                {
                    const Vector2 velocity = {
                        (mouse.x - previousMouse.x)*(float)SIMULATION_SIZE/
                            simulationRectangle.width*(float)TARGET_FRAME_RATE*VELOCITY_SOURCE_GAIN,
                        (previousMouse.y - mouse.y)*(float)SIMULATION_SIZE/
                            simulationRectangle.height*(float)TARGET_FRAME_RATE*VELOCITY_SOURCE_GAIN
                    };
                    if (!FluidSimAddVelocitySource(simulation, gridPosition, velocity, BRUSH_RADIUS))
                    {
                        TraceLog(LOG_ERROR, "Could not apply the velocity source.");
                        goto cleanup;
                    }
                }
                hasPreviousMouse = true;
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
                !FluidSimAddDyeSource(simulation, gridPosition, DYE_SOURCE_AMOUNT, BRUSH_RADIUS))
            {
                TraceLog(LOG_ERROR, "Could not apply the dye source.");
                goto cleanup;
            }
            previousMouse = mouse;
        }
        else
        {
            hasPreviousMouse = false;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            hasPreviousMouse = false;
        }

        if (!paused && !FluidSimStep(simulation))
        {
            TraceLog(LOG_ERROR, "The fluid simulation step failed.");
            goto cleanup;
        }

        BeginDrawing();
        ClearBackground(BACKGROUND_COLOR);
        Texture2D texture = FluidSimGetTexture(simulation);
        if (simulationRectangle.width > 0.0f)
        {
            const Rectangle source = {0.0f, 0.0f, (float)texture.width, -(float)texture.height};
            DrawTexturePro(texture, source, simulationRectangle, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        }
        DrawText("SPACE pause  C clear  Left drag stir  Right drag dye",
                 12, 12, 18, RAYWHITE);
        DrawText(paused ? "PAUSED" : TextFormat("%d FPS", GetFPS()),
                 12, 36, 18, paused ? GOLD : RAYWHITE);
        EndDrawing();

        measuredSeconds += GetFrameTime();
        measuredFrames++;
        if (measuredSeconds >= 5.0)
        {
            TraceLog(LOG_INFO, "Full-frame average: %.1f FPS (%.2f ms, %u frames)",
                     (double)measuredFrames/measuredSeconds,
                     1000.0*measuredSeconds/measuredFrames, measuredFrames);
            measuredSeconds = 0.0;
            measuredFrames = 0;
        }
    }

    exitCode = 0;

cleanup:
    FluidSimDestroy(simulation);
    CloseWindow();
    return exitCode;
}
