#ifndef MOUSE_MAPPING_H
#define MOUSE_MAPPING_H

#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct MouseGridTrail
{
    Vector2 previousPosition;
    bool hasPreviousPosition;
} MouseGridTrail;

typedef bool (*MouseGridTrailSourceHandler)(Vector2 position, void *context);

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

static bool UpdateMouseGridTrail(MouseGridTrail *trail, bool active, Vector2 position,
                                 unsigned int gridWidth, unsigned int gridHeight,
                                 float maximumSpacing, MouseGridTrailSourceHandler addSource,
                                 void *context)
{
    if (trail == NULL)
    {
        return false;
    }
    if (!active)
    {
        trail->hasPreviousPosition = false;
        return true;
    }
    if ((gridWidth == 0) || (gridHeight == 0) || !isfinite(maximumSpacing) ||
        (maximumSpacing <= 0.0f) || (addSource == NULL))
    {
        return false;
    }
    if (!isfinite(position.x) || !isfinite(position.y) ||
        (position.x < 0.0f) || (position.y < 0.0f) ||
        (position.x >= (float)gridWidth) || (position.y >= (float)gridHeight))
    {
        trail->hasPreviousPosition = false;
        return true;
    }

    if (!trail->hasPreviousPosition)
    {
        if (!addSource(position, context))
        {
            return false;
        }
        trail->previousPosition = position;
        trail->hasPreviousPosition = true;
        return true;
    }

    const Vector2 previous = trail->previousPosition;
    const float dx = position.x - previous.x;
    const float dy = position.y - previous.y;
    const float distance = sqrtf(dx*dx + dy*dy);
    const size_t steps = (size_t)ceilf(distance/maximumSpacing);
    for (size_t step = 1; step <= steps; step++)
    {
        const float fraction = (float)step/(float)steps;
        const Vector2 sample = {
            previous.x + dx*fraction,
            previous.y + dy*fraction
        };
        if (!addSource(sample, context))
        {
            return false;
        }
    }
    trail->previousPosition = position;
    return true;
}

#endif
