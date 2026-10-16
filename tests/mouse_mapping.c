#include "mouse_mapping.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct TrailCapture
{
    Vector2 positions[16];
    size_t count;
} TrailCapture;

static bool CaptureTrailPosition(Vector2 position, void *context)
{
    TrailCapture *capture = context;
    if (capture->count == sizeof(capture->positions)/sizeof(capture->positions[0]))
    {
        return false;
    }
    capture->positions[capture->count++] = position;
    return true;
}

int main(void)
{
    const Rectangle simulationRectangle = {100.0f, 50.0f, 200.0f, 200.0f};
    Vector2 topQuarter = {0};
    Vector2 bottomQuarter = {0};
    if (!MapMouseToGrid((Vector2){150.0f, 75.0f}, simulationRectangle,
                        1024.0f, 1024.0f, &topQuarter) ||
        !MapMouseToGrid((Vector2){150.0f, 225.0f}, simulationRectangle,
                        1024.0f, 1024.0f, &bottomQuarter))
    {
        fputs("Could not map test mouse coordinates inside the simulation rectangle.\n", stderr);
        return 1;
    }
    if ((topQuarter.x != 256.0f) || (topQuarter.y != 896.0f) ||
        (bottomQuarter.x != 256.0f) || (bottomQuarter.y != 128.0f))
    {
        fprintf(stderr, "Mouse mapping is incorrect: top=(%g,%g), bottom=(%g,%g)\n",
                (double)topQuarter.x, (double)topQuarter.y,
                (double)bottomQuarter.x, (double)bottomQuarter.y);
        return 1;
    }

    Vector2 topEdge = {0};
    if (!MapMouseToGrid((Vector2){150.0f, 50.0f}, simulationRectangle,
                        1024.0f, 1024.0f, &topEdge) ||
        (topEdge.y != 1023.0f))
    {
        fprintf(stderr, "The viewport's top edge mapped outside the active grid: %g\n",
                (double)topEdge.y);
        return 1;
    }

    const Vector2 outsideViewport[] = {
        {99.0f, 100.0f},
        {300.0f, 100.0f},
        {150.0f, 49.0f},
        {150.0f, 250.0f}
    };
    for (size_t i = 0; i < sizeof(outsideViewport)/sizeof(outsideViewport[0]); i++)
    {
        Vector2 unchanged = {42.0f, 84.0f};
        if (MapMouseToGrid(outsideViewport[i], simulationRectangle, 1024.0f, 1024.0f,
                           &unchanged) ||
            (unchanged.x != 42.0f) || (unchanged.y != 84.0f))
        {
            fputs("Mouse coordinates outside the square viewport were accepted.\n", stderr);
            return 1;
        }
    }

    MouseGridTrail trail = {0};
    TrailCapture capture = {0};
    if (!UpdateMouseGridTrail(&trail, true, (Vector2){2.0f, 16.0f}, 32, 32, 8.0f,
                              CaptureTrailPosition, &capture) ||
        !UpdateMouseGridTrail(&trail, true, (Vector2){29.0f, 16.0f}, 32, 32, 8.0f,
                              CaptureTrailPosition, &capture) ||
        (capture.count <= 2))
    {
        fputs("A long mouse segment did not produce intermediate dye trail sources.\n", stderr);
        return 1;
    }
    const size_t longStrokeSourceCount = capture.count;
    if ((capture.positions[0].x != 2.0f) || (capture.positions[0].y != 16.0f) ||
        (capture.positions[capture.count - 1].x != 29.0f) ||
        (capture.positions[capture.count - 1].y != 16.0f))
    {
        fputs("A dye trail omitted one of its pointer sample positions.\n", stderr);
        return 1;
    }
    for (size_t i = 0; i < capture.count; i++)
    {
        const Vector2 position = capture.positions[i];
        if ((position.x < 0.0f) || (position.x >= 32.0f) ||
            (position.y < 0.0f) || (position.y >= 32.0f))
        {
            fputs("A trail source escaped the active simulation grid.\n", stderr);
            return 1;
        }
        if (i > 0)
        {
            const float dx = position.x - capture.positions[i - 1].x;
            const float dy = position.y - capture.positions[i - 1].y;
            if (sqrtf(dx*dx + dy*dy) > 8.0001f)
            {
                fputs("A long mouse segment contains an over-spaced trail gap.\n", stderr);
                return 1;
            }
        }
    }

    if (!UpdateMouseGridTrail(&trail, false, (Vector2){0.0f, 0.0f}, 32, 32, 8.0f,
                              CaptureTrailPosition, &capture) ||
        !UpdateMouseGridTrail(&trail, true, (Vector2){31.0f, 31.0f}, 32, 32, 8.0f,
                              CaptureTrailPosition, &capture) ||
        (capture.count != longStrokeSourceCount + 1) ||
        (capture.positions[capture.count - 1].x != 31.0f) ||
        (capture.positions[capture.count - 1].y != 31.0f))
    {
        fputs("A mouse trail did not restart at the re-entry position.\n", stderr);
        return 1;
    }

    puts("Mouse mapping preserves displayed orientation and rejects letterbox coordinates.");
    return 0;
}
