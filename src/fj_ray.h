// Copyright (c) 2011-2017 Hiroshi Tsubokawa
// See LICENSE and README

#ifndef FJ_RAY_H
#define FJ_RAY_H

#include "fj_vector.h"
#include "fj_types.h"

namespace fj {

class Ray {
public:
  Ray() : orig(), dir(0, 0, 1), tmin(.001), tmax(1000) {}
  ~Ray() {}

  Vector orig;
  Vector dir;

  Real tmin;
  Real tmax;
};

inline Vector RayPointAt(const Ray &ray, Real t)
{
  return ray.orig + t * ray.dir;
}

inline bool RayInRange(const Ray &ray, Real t)
{
  return ray.tmin <= t && t <= ray.tmax;
}

} // namespace xxx

#endif // FJ_XXX_H
