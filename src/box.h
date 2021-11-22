#ifndef __BOX_H__
#define __BOX_H__

int box_area(BoxPtr box);

Bool box_get_intersect(BoxPtr dest, BoxPtr a, BoxPtr b);

#endif
