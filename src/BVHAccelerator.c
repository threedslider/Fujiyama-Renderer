/*
Copyright (c) 2011-2013 Hiroshi Tsubokawa
See LICENSE and README
*/

#include "BVHAccelerator.h"
#include "Intersection.h"
#include "PrimitiveSet.h"
#include "Accelerator.h"
#include "Numeric.h"
#include "Memory.h"
#include "Box.h"
#include "Ray.h"

#include <assert.h>
#include <float.h>

static const char ACCELERATOR_NAME[] = "BVH";

enum { BVH_STACKSIZE = 64 };
enum {
	HIT_NONE = 0,
	HIT_LEFT = 1,
	HIT_RIGHT = 2,
	HIT_BOTH = 3
};

struct Primitive {
	struct Box bounds;
	double centroid[3];
	int index;
};

struct BVHNode {
	struct BVHNode *left;
	struct BVHNode *right;
	struct Box bounds;
	int prim_id;
};

struct BVHNodeStack {
	int depth;
	const struct BVHNode *node[BVH_STACKSIZE];
};

struct BVHAccelerator {
	struct BVHNode *root;
};

static DerivedAccelerator new_bvh_accel(void);
static void free_bvh_accel(DerivedAccelerator derived);
static int build_bvh_accel(DerivedAccelerator derived,
		const struct PrimitiveSet *primset);
static int intersect_bvh_accel(DerivedAccelerator derived,
		const struct PrimitiveSet *primset, double time, const struct Ray *ray,
		struct Intersection *isect);
static int intersect_bvh_recursive(const struct PrimitiveSet *primset,
		const struct BVHNode *node, double time, const struct Ray *ray,
		struct Intersection *isect);
static int intersect_bvh_loop(const struct PrimitiveSet *primset,
		const struct BVHNode *root, double time, const struct Ray *ray,
		struct Intersection *isect);
static const char *get_bvh_accel_name(void);

static struct BVHNode *new_bvhnode(void);
static void free_bvhnode_recursive(struct BVHNode *node);
static struct BVHNode *build_bvh(struct Primitive **prims, int begin, int end, int axis);
static int is_bvh_leaf(const struct BVHNode *node);
static int find_median(struct Primitive **prims, int begin, int end, int axis);

static int compare_double(double x, double y);
static int primitive_compare_x(const void *a, const void *b);
static int primitive_compare_y(const void *a, const void *b);
static int primitive_compare_z(const void *a, const void *b);

static int is_empty(const struct BVHNodeStack *stack);
static void push_node(struct BVHNodeStack *stack, const struct BVHNode *node);
static const struct BVHNode *pop_node(struct BVHNodeStack *stack);

/* TODO move this somewhere */
static int prim_ray_intersect(const struct PrimitiveSet *primset, int prim_id,
		double time, const struct Ray *ray, struct Intersection *isect);
static void swap_isect_ptr(struct Intersection **isect0, struct Intersection **isect1);

void GetBVHAcceleratorFunction(struct Accelerator *acc)
{
	AccSetDerivedFunctions(acc,
			new_bvh_accel,
			free_bvh_accel,
			build_bvh_accel,
			intersect_bvh_accel,
			get_bvh_accel_name);
}

static DerivedAccelerator new_bvh_accel(void)
{
	struct BVHAccelerator *bvh = MEM_ALLOC(struct BVHAccelerator);

	if (bvh == NULL)
		return NULL;

	bvh->root = NULL;

	return (DerivedAccelerator) bvh;
}

static void free_bvh_accel(DerivedAccelerator derived)
{
	struct BVHAccelerator *bvh = (struct BVHAccelerator *) derived;

	free_bvhnode_recursive(bvh->root);

	MEM_FREE(bvh);
}

static int build_bvh_accel(DerivedAccelerator derived,
		const struct PrimitiveSet *primset)
{
	struct BVHAccelerator *bvh = (struct BVHAccelerator *) derived;
	struct Primitive *prims = NULL;
	struct Primitive **primptrs = NULL;

	const int NPRIMS = PrmGetPrimitiveCount(primset);
	int i;

	prims = MEM_ALLOC_ARRAY(struct Primitive, NPRIMS);
	if (prims == NULL)
		return -1;

	primptrs = MEM_ALLOC_ARRAY(struct Primitive *, NPRIMS);
	if (primptrs == NULL) {
		MEM_FREE(prims);
		return -1;
	}

	for (i = 0; i < NPRIMS; i++) {
		PrmGetPrimitiveBounds(primset, i, &prims[i].bounds);
		prims[i].centroid[0] = (prims[i].bounds.max.x + prims[i].bounds.min.x) / 2;
		prims[i].centroid[1] = (prims[i].bounds.max.y + prims[i].bounds.min.y) / 2;
		prims[i].centroid[2] = (prims[i].bounds.max.z + prims[i].bounds.min.z) / 2;
		prims[i].index = i;

		primptrs[i] = &prims[i];
	}

	bvh->root = build_bvh(primptrs, 0, NPRIMS, 0);
	if (bvh->root == NULL) {
		MEM_FREE(primptrs);
		MEM_FREE(prims);
		return -1;
	}

	MEM_FREE(primptrs);
	MEM_FREE(prims);
	return 0;
}

static int intersect_bvh_accel(DerivedAccelerator derived,
		const struct PrimitiveSet *primset, double time, const struct Ray *ray,
		struct Intersection *isect)
{
	const struct BVHAccelerator *bvh = (const struct BVHAccelerator *) derived;

	if (1)
		return intersect_bvh_loop(primset, bvh->root, time, ray, isect);
	else
		return intersect_bvh_recursive(primset, bvh->root, time, ray, isect);
}

static int intersect_bvh_recursive(const struct PrimitiveSet *primset,
		const struct BVHNode *node, double time, const struct Ray *ray,
		struct Intersection *isect)
{
	struct Intersection isect_left, isect_right;
	double boxhit_tmin;
	double boxhit_tmax;
	int hit_left, hit_right;

	const int hit = BoxRayIntersect(&node->bounds,
			&ray->orig, &ray->dir, ray->tmin, ray->tmax,
			&boxhit_tmin, &boxhit_tmax);

	if (!hit) {
		return 0;
	}

	if (is_bvh_leaf(node)) {
		return prim_ray_intersect(primset, node->prim_id, time, ray, isect);
	}

	isect_left.t_hit = FLT_MAX;
	hit_left  = intersect_bvh_recursive(primset, node->left,  time, ray, &isect_left);
	isect_right.t_hit = FLT_MAX;
	hit_right = intersect_bvh_recursive(primset, node->right, time, ray, &isect_right);

	if (isect_left.t_hit < ray->tmin)
		isect_left.t_hit = FLT_MAX;
	if (isect_right.t_hit < ray->tmin)
		isect_right.t_hit = FLT_MAX;

	if (isect_left.t_hit < isect_right.t_hit) {
		*isect = isect_left;
	} else if (isect_right.t_hit < isect_left.t_hit) {
		*isect = isect_right;
	}

	return (hit_left || hit_right);
}

static int intersect_bvh_loop(const struct PrimitiveSet *primset,
		const struct BVHNode *root, double time, const struct Ray *ray,
		struct Intersection *isect)
{
	int hit = 0;
	int hittmp = 0;
	int whichhit;
	int hit_left, hit_right;
	double boxhit_tmin, boxhit_tmax;
	struct Intersection isect_candidates[2];
	struct Intersection *isect_min, *isect_tmp;

	const struct BVHNode *node = root;
	struct BVHNodeStack stack = {0, {NULL}};

	isect_min = &isect_candidates[0];
	isect_tmp = &isect_candidates[1];
	isect_min->t_hit = FLT_MAX;

	for (;;) {
		if (is_bvh_leaf(node)) {
			hittmp = prim_ray_intersect(primset, node->prim_id, time, ray, isect_tmp);
			if (hittmp && isect_tmp->t_hit < isect_min->t_hit) {
				swap_isect_ptr(&isect_min, &isect_tmp);
				hit = hittmp;
			}

			if (is_empty(&stack))
				goto loop_exit;
			node = pop_node(&stack);
			continue;
		}

		hit_left = BoxRayIntersect(&node->left->bounds,
				&ray->orig, &ray->dir, ray->tmin, ray->tmax,
				&boxhit_tmin, &boxhit_tmax);

		hit_right = BoxRayIntersect(&node->right->bounds,
				&ray->orig, &ray->dir, ray->tmin, ray->tmax,
				&boxhit_tmin, &boxhit_tmax);

		whichhit = HIT_NONE;
		whichhit |= hit_left  ? HIT_LEFT :  HIT_NONE;
		whichhit |= hit_right ? HIT_RIGHT : HIT_NONE;

		switch (whichhit) {
		case HIT_NONE:
			if (is_empty(&stack))
				goto loop_exit;
			node = pop_node(&stack);
			break;

		case HIT_LEFT:
			node = node->left;
			break;

		case HIT_RIGHT:
			node = node->right;
			break;

		case HIT_BOTH:
			push_node(&stack, node->right);
			node = node->left;
			break;

		default:
			assert(!"invalid whichhit");
			break;
		}
	}
loop_exit:

	if (hit) {
		*isect = *isect_min;
	}

	return hit;
}

static struct BVHNode *build_bvh(struct Primitive **primptrs, int begin, int end, int axis)
{
	const int NPRIMS = end - begin;
	int median = 0;
	int new_axis = 0;
	int (*primitive_compare)(const void *, const void *) = NULL;

	struct BVHNode *node = new_bvhnode();
	if (node == NULL)
		return NULL;

	if (NPRIMS == 1) {
		node->prim_id = primptrs[begin]->index;
		node->bounds = primptrs[begin]->bounds;
		return node;
	}

	switch (axis) {
		case 0:
			primitive_compare = primitive_compare_x;
			new_axis = 1;
			break;
		case 1:
			primitive_compare = primitive_compare_y;
			new_axis = 2;
			break;
		case 2:
			primitive_compare = primitive_compare_z;
			new_axis = 0;
			break;
		default:
			assert(!"invalid axis");
			break;
	}

	qsort(primptrs + begin, NPRIMS, sizeof(struct Primitive *), primitive_compare);
	median = find_median(primptrs, begin, end, axis);

	node->left  = build_bvh(primptrs, begin, median, new_axis);
	if (node->left == NULL)
		return NULL;

	node->right = build_bvh(primptrs, median, end, new_axis);
	if (node->right == NULL)
		return NULL;

	node->bounds = node->left->bounds;
	BoxAddBox(&node->bounds, &node->right->bounds);

	return node;
}

static struct BVHNode *new_bvhnode(void)
{
	struct BVHNode *node = MEM_ALLOC(struct BVHNode);

	if (node == NULL)
		return NULL;

	node->left = NULL;
	node->right = NULL;
	node->prim_id = -1;
	BOX3_SET(&node->bounds, 0, 0, 0, 0, 0, 0);

	return node;
}

static void free_bvhnode_recursive(struct BVHNode *node)
{
	if (node == NULL)
		return;

	if (is_bvh_leaf(node)) {
		MEM_FREE(node);
		return;
	}

	assert(node->left != NULL);
	assert(node->right != NULL);
	assert(node->prim_id == -1);

	free_bvhnode_recursive(node->left);
	free_bvhnode_recursive(node->right);

	MEM_FREE(node);
}

static int is_bvh_leaf(const struct BVHNode *node)
{
	return (
		node->left == NULL &&
		node->right == NULL &&
		node->prim_id != -1);
}

static int find_median(struct Primitive **prims, int begin, int end, int axis)
{
	int low, high, mid;
	double key;

	assert(axis >= 0 && axis <= 2);

	low = begin;
	high = end - 1;
	mid = -1;
	key = (prims[low]->centroid[axis] + prims[high]->centroid[axis]) / 2;

	while (low != mid) {
		mid = (low + high) / 2;
		if (key < prims[mid]->centroid[axis])
			high = mid;
		else if (prims[mid]->centroid[axis] < key)
			low = mid;
		else
			break;
	}

	return mid + 1;
}

static int compare_double(double x, double y)
{
	if (x > y)
		return 1;
	else if (x < y)
		return -1;
	else
		return 0;
}

static int primitive_compare_x(const void *a, const void *b)
{
	struct Primitive **A = (struct Primitive **) a;
	struct Primitive **B = (struct Primitive **) b;
	return compare_double((*A)->centroid[0], (*B)->centroid[0]);
}

static int primitive_compare_y(const void *a, const void *b)
{
	struct Primitive **A = (struct Primitive **) a;
	struct Primitive **B = (struct Primitive **) b;
	return compare_double((*A)->centroid[1], (*B)->centroid[1]);
}

static int primitive_compare_z(const void *a, const void *b)
{
	struct Primitive **A = (struct Primitive **) a;
	struct Primitive **B = (struct Primitive **) b;
	return compare_double((*A)->centroid[2], (*B)->centroid[2]);
}

static int is_empty(const struct BVHNodeStack *stack)
{
	return stack->depth == 0;
}

static void push_node(struct BVHNodeStack *stack, const struct BVHNode *node)
{
	stack->node[stack->depth++] = node;
	assert(stack->depth < BVH_STACKSIZE);
}

static const struct BVHNode *pop_node(struct BVHNodeStack *stack)
{
	assert(!is_empty(stack));
	return stack->node[--stack->depth];
}

static const char *get_bvh_accel_name(void)
{
	return ACCELERATOR_NAME;
}

static int prim_ray_intersect(const struct PrimitiveSet *primset, int prim_id,
		double time, const struct Ray *ray, struct Intersection *isect)
{
	const int hit = PrmRayIntersect(primset, prim_id, time, ray, isect);

	if (!hit) {
		isect->t_hit = FLT_MAX;
		return 0;
	}

	if (isect->t_hit < ray->tmin || ray->tmax < isect->t_hit) {
		isect->t_hit = FLT_MAX;
		return 0;
	}

	return 1;
}

static void swap_isect_ptr(struct Intersection **isect0, struct Intersection **isect1)
{
	struct Intersection *isect_swp = *isect0;
	*isect0 = *isect1;
	*isect1 = isect_swp;
}
