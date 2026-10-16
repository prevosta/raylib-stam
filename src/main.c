#include "fluid_sim.h"
#include "mouse_mapping.h"
#include "performance_report.h"
#include "rlgl.h"
#include "simulation_tuning.h"

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
    TARGET_FRAME_RATE = 60,
    BENCHMARK_WARMUP_SECONDS = 5,
    BENCHMARK_SAMPLE_SECONDS = 10
};

static const float VELOCITY_VISCOSITY = SIMULATION_VISCOSITY;
static const float BENCHMARK_BRUSH_RADIUS = SIMULATION_BRUSH_RADIUS;
static const float VELOCITY_SOURCE_GAIN = SIMULATION_VELOCITY_SOURCE_GAIN;
static const float DYE_SOURCE_AMOUNT = SIMULATION_DYE_SOURCE_AMOUNT;
static const double BRUSH_INDICATOR_DURATION = 0.75;
static const Color BACKGROUND_COLOR = {5, 7, 9, 255};

typedef struct BrushContext
{
    FluidSim *simulation;
    float radius;
} BrushContext;

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

static bool AddDyeTrailSource(Vector2 position, void *context)
{
    BrushContext *brush = context;
    return FluidSimAddDyeSource(brush->simulation, position, DYE_SOURCE_AMOUNT, brush->radius);
}

int main(int argc, char **argv)
{
    if ((argc > 1) && ((argc != 2) || (strcmp(argv[1], "--benchmark") != 0)))
    {
        fputs("Usage: raylib_stam.exe [--benchmark]\n", stderr);
        return 2;
    }
    const bool benchmarkMode = (argc == 2);
    FluidSim *simulation = NULL;
    int exitCode = 1;
    bool paused = false;
    bool hasPreviousMouse = false;
    MouseGridTrail dyeTrail = {0};
    BrushContext brush = {0};
    bool benchmarkWarmupComplete = !benchmarkMode;
    Vector2 previousMouse = {0};
    double brushIndicatorUntil = 0.0;
    double benchmarkWarmupSeconds = 0.0;
    double measuredSeconds = 0.0;
    unsigned int measuredFrames = 0;

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (benchmarkMode ? 0 : FLAG_VSYNC_HINT));
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
    const char *vendor = (vendorBytes != NULL) ? (const char *)vendorBytes : NULL;
    if (PerformanceReportEvaluate(renderer, vendor, PERFORMANCE_REPORT_TARGET_FPS) ==
        PERFORMANCE_REPORT_NOT_TARGET_ADAPTER)
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
        .solverIterations = SIMULATION_SOLVER_ITERATIONS,
        .viscosity = VELOCITY_VISCOSITY,
        .shaderDirectory = "shaders"
    };
    char errorMessage[512] = {0};
    simulation = FluidSimCreate(&config, errorMessage, sizeof(errorMessage));
    if (simulation == NULL)
    {
        TraceLog(LOG_ERROR, "Could not initialize the fluid simulation: %s",
                 errorMessage[0] != '\0' ? errorMessage : "unspecified error");
        goto cleanup;
    }
    brush.simulation = simulation;
    brush.radius = SIMULATION_INITIAL_BRUSH_RADIUS;

    SetTargetFPS(benchmarkMode ? 0 : TARGET_FRAME_RATE);
    if (benchmarkMode)
    {
        TraceLog(LOG_INFO,
                 "Full-solver benchmark: uncapped rendered loop; %d-second warm-up, then one "
                 "%d-second sample with a fixed center velocity and dye source.",
                 BENCHMARK_WARMUP_SECONDS, BENCHMARK_SAMPLE_SECONDS);
    }
    while (!WindowShouldClose())
    {
        if (!benchmarkMode && IsKeyPressed(KEY_SPACE))
        {
            paused = !paused;
            hasPreviousMouse = false;
        }
        if (!benchmarkMode && IsKeyPressed(KEY_C) && !FluidSimClear(simulation))
        {
            TraceLog(LOG_ERROR, "Could not clear the simulation fields.");
            goto cleanup;
        }

        Rectangle simulationRectangle = {0};
        Vector2 mouse = GetMousePosition();
        Vector2 gridPosition = {0};
        const float wheelMove = benchmarkMode ? 0.0f : GetMouseWheelMove();
        const bool mouseInside = GetSimulationRectangle(GetScreenWidth(), GetScreenHeight(),
                                                         &simulationRectangle) &&
                                 MapMouseToGrid(mouse, simulationRectangle, (float)SIMULATION_SIZE,
                                                (float)SIMULATION_SIZE, &gridPosition);
        if (!benchmarkMode && mouseInside && (wheelMove != 0.0f))
        {
            float radius = brush.radius + wheelMove*SIMULATION_BRUSH_RADIUS_SCROLL_STEP;
            if (radius < SIMULATION_BRUSH_RADIUS_MIN)
            {
                radius = SIMULATION_BRUSH_RADIUS_MIN;
            }
            else if (radius > SIMULATION_BRUSH_RADIUS_MAX)
            {
                radius = SIMULATION_BRUSH_RADIUS_MAX;
            }
            if (radius != brush.radius)
            {
                brush.radius = radius;
                brushIndicatorUntil = GetTime() + BRUSH_INDICATOR_DURATION;
            }
        }
        if (!benchmarkMode && !paused && mouseInside)
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
                    if (!FluidSimAddVelocitySource(simulation, gridPosition, velocity, brush.radius))
                    {
                        TraceLog(LOG_ERROR, "Could not apply the velocity source.");
                        goto cleanup;
                    }
                }
                hasPreviousMouse = true;
            }
            previousMouse = mouse;
        }
        else if (!benchmarkMode)
        {
            hasPreviousMouse = false;
        }
        if (!benchmarkMode &&
            !UpdateMouseGridTrail(&dyeTrail,
                                  !paused && mouseInside &&
                                      IsMouseButtonDown(MOUSE_BUTTON_RIGHT),
                                  gridPosition, SIMULATION_SIZE, SIMULATION_SIZE,
                                  brush.radius, AddDyeTrailSource, &brush))
        {
            TraceLog(LOG_ERROR, "Could not apply the dye trail source.");
            goto cleanup;
        }
        if (!benchmarkMode && !IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            hasPreviousMouse = false;
        }

        if (benchmarkMode)
        {
            const Vector2 sourcePosition = {
                (float)SIMULATION_SIZE*0.5f,
                (float)SIMULATION_SIZE*0.5f
            };
            const Vector2 sourceVelocity = {
                VELOCITY_SOURCE_GAIN*(float)TARGET_FRAME_RATE,
                0.0f
            };
            if (!FluidSimAddVelocitySource(simulation, sourcePosition, sourceVelocity,
                                           BENCHMARK_BRUSH_RADIUS) ||
                !FluidSimAddDyeSource(simulation, sourcePosition, DYE_SOURCE_AMOUNT,
                                      BENCHMARK_BRUSH_RADIUS))
            {
                TraceLog(LOG_ERROR, "Could not apply benchmark sources.");
                goto cleanup;
            }
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
        const double indicatorSecondsRemaining = brushIndicatorUntil - GetTime();
        if (!benchmarkMode && mouseInside && (indicatorSecondsRemaining > 0.0))
        {
            const float indicatorRadius = brush.radius*simulationRectangle.width/
                                          (float)SIMULATION_SIZE;
            const Color indicatorColor = Fade(
                GRAY, (float)(indicatorSecondsRemaining/BRUSH_INDICATOR_DURATION));
            DrawCircleLinesV(mouse, indicatorRadius, indicatorColor);
        }
        DrawText(benchmarkMode ? "BENCHMARK: fixed center sources, uncapped"
                               : "SPACE pause  C clear  Left drag stir  Right drag dye  Scroll size",
                 12, 12, 18, RAYWHITE);
        DrawText(paused ? "PAUSED" : TextFormat("%d FPS", GetFPS()),
                 12, 36, 18, paused ? GOLD : RAYWHITE);
        EndDrawing();

        const double frameSeconds = GetFrameTime();
        if (!benchmarkWarmupComplete)
        {
            benchmarkWarmupSeconds += frameSeconds;
            if (benchmarkWarmupSeconds >= BENCHMARK_WARMUP_SECONDS)
            {
                benchmarkWarmupComplete = true;
            }
        }
        else
        {
            measuredSeconds += frameSeconds;
            measuredFrames++;
            const double sampleDuration = benchmarkMode ? BENCHMARK_SAMPLE_SECONDS : 5.0;
            if (measuredSeconds >= sampleDuration)
            {
                const double averageFramesPerSecond = (double)measuredFrames/measuredSeconds;
                const PerformanceReportStatus reportStatus =
                    PerformanceReportEvaluate(renderer, vendor, averageFramesPerSecond);
                TraceLog(LOG_INFO, "Full-rendered solver sample: %.3f FPS, %.3f ms/frame; %u frames "
                                   "over %.3f s; acceptance=%s",
                         averageFramesPerSecond, 1000.0*measuredSeconds/measuredFrames,
                         measuredFrames, measuredSeconds, PerformanceReportStatusName(reportStatus));
                TraceLog(LOG_INFO, "Configuration: grid=%ux%u, solverIterations=%u, viscosity=%.8g, "
                                   "brushRadius=%.2f, velocitySourceGain=%.2f, dyeSourceAmount=%.2f",
                         (unsigned int)SIMULATION_SIZE, (unsigned int)SIMULATION_SIZE,
                         config.solverIterations, (double)config.viscosity,
                         (double)BENCHMARK_BRUSH_RADIUS,
                         (double)VELOCITY_SOURCE_GAIN, (double)DYE_SOURCE_AMOUNT);
                TraceLog(LOG_INFO, "Renderer: %s; vendor: %s; OpenGL version/driver: %s",
                         renderer, (vendor != NULL) ? vendor : "unknown",
                         (versionBytes != NULL) ? (const char *)versionBytes : "unknown");
                measuredSeconds = 0.0;
                measuredFrames = 0;
                if (benchmarkMode)
                {
                    exitCode = 0;
                    goto cleanup;
                }
            }
        }
    }

    exitCode = 0;

cleanup:
    FluidSimDestroy(simulation);
    CloseWindow();
    return exitCode;
}
