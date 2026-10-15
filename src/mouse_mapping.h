#ifndef MOUSE_MAPPING_H
#define MOUSE_MAPPING_H

#include "raylib.h"

#include <stddef.h>

static bool MapMouseToGrid(Vector2 mouse, Rectangle rectangle, float gridWidth, float gridHeight,
                           Vector2 *grid)
{
    if ((grid == NULL) || (mouse.x < rectangle.x) ||
        (mouse.x >= rectangle.x + rectangle.width) ||
        (mouse.y < rectangle.y) || (mouse.y >= rectangle.y + rectangle.height))
    {
        return false;
    }
    grid->x = (mouse.x - rectangle.x)*gridWidth/rectangle.width;
    grid->y = (rectangle.y + rectangle.height - mouse.y)*gridHeight/rectangle.height;
    if (grid->y >= gridHeight)
    {
        grid->y = gridHeight - 1.0f;
    }
    return true;
}

#endif
