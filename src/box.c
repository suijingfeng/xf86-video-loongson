
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <xf86.h>

#include "box.h"

int box_area(BoxPtr box)
{
    return (int)(box->x2 - box->x1) * (int)(box->y2 - box->y1);
}

Bool box_get_intersect(BoxPtr dest, BoxPtr a, BoxPtr b)
{
    dest->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    dest->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    if (dest->x1 >= dest->x2)
    {
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
        return FALSE;
    }

    dest->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    dest->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    if (dest->y1 >= dest->y2)
    {
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
        return FALSE;
    }

    return TRUE;
}
