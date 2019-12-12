#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
	p -> x += x;
	p -> y += y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	return sqrt(pow((p1 -> x - p2 -> x), 2) + pow((p1 -> y - p2 -> y), 2));
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double dist1 = sqrt(pow((p1 -> x), 2) + pow((p1 -> y), 2));
	double dist2 = sqrt(pow((p2 -> x), 2) + pow((p2 -> y), 2));
	if (dist1 < dist2)
		return -1;
	else if (dist1 == dist2)
		return 0;
	else
		return 1;
}
