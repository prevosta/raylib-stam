#include "mouse_mapping.h"

#include <stddef.h>
#include <stdio.h>

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

    puts("Mouse mapping preserves displayed orientation and rejects letterbox coordinates.");
    return 0;
}
