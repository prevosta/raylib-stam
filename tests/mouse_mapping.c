#include "mouse_mapping.h"

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
    if ((topQuarter.y != 896.0f) || (bottomQuarter.y != 128.0f))
    {
        fprintf(stderr, "Mouse Y mapping is inverted: top=%g, bottom=%g\n",
                (double)topQuarter.y, (double)bottomQuarter.y);
        return 1;
    }
    puts("Mouse Y maps to the matching displayed grid row.");
    return 0;
}
