// Copyright (c) 2011-2020 Hiroshi Tsubokawa
// See LICENSE and README

#include "fj_shading.h"
#include "fj_volume_accelerator.h"
#include "fj_object_instance.h"
#include "fj_intersection.h"
#include "fj_object_group.h"
#include "fj_accelerator.h"
#include "fj_interval.h"
#include "fj_numeric.h"
#include "fj_texture.h"
#include "fj_shader.h"
#include "fj_volume.h"
#include "fj_light.h"
#include "fj_ray.h"

#include <cassert>
#include <cstdio>
#include <cfloat>
#include <cmath>

namespace fj {

static const Color NO_SHADER_COLOR(.5, 1., 0.);

static int has_reached_bounce_limit(const TraceContext *cxt);
static int shadow_ray_has_reached_opcity_limit(const TraceContext *cxt, float opac);
static void setup_ray(const Vector *ray_orig, const Vector *ray_dir,
    double ray_tmin, double ray_tmax,
    Ray *ray);
static void setup_surface_input(
    const Intersection *isect,
    const Ray *ray,
    SurfaceInput *in);

static int trace_surface(const TraceContext *cxt, const Ray &ray,
    Color4 *out_rgba, double *t_hit);
static int raymarch_volume(const TraceContext *cxt, const Ray *ray,
    Color4 *out_rgba);

void SlFaceforward(const Vector *I, const Vector *N, Vector *Nf)
{
  if (Dot(*I, *N) < 0) {
    *Nf = *N;
    return;
  }
  Nf->x = -N->x;
  Nf->y = -N->y;
  Nf->z = -N->z;
}

double SlFresnel(const Vector *I, const Vector *N, double ior)
{
  double k2 = 0;
  double F0 = 0;
  double eta = 0;
  double cos = 0;

  // dot(-I, N)
  cos = -1 * Dot(*I, *N);
  if (cos > 0) {
    eta = ior;
  } else {
    eta = 1./ior;
    cos *= -1;
  }

  k2 = .0;
  F0 = ((1.-eta) * (1.-eta) + k2) / ((1.+eta) * (1.+eta) + k2);

  return F0 + (1. - F0) * pow(1. - cos, 5.);
}

double SlPhong(const Vector *I, const Vector *N, const Vector *L,
    double roughness)
{
  double spec = 0;
  Vector Lrefl;

  SlReflect(L, N, &Lrefl);

  spec = Dot(*I, Lrefl);
  spec = Max(0., spec);
  spec = pow(spec, 1/Max(.001, roughness));

  return spec;
}

void SlReflect(const Vector *I, const Vector *N, Vector *R)
{
  // dot(-I, N)
  const double cos = -1 * Dot(*I, *N);

  R->x = I->x + 2 * cos * N->x;
  R->y = I->y + 2 * cos * N->y;
  R->z = I->z + 2 * cos * N->z;
}

void SlRefract(const Vector *I, const Vector *N, double ior,
    Vector *T)
{
  Vector n;
  double radicand = 0;
  double ncoeff = 0;
  double cos1 = 0;
  double eta = 0;

  // dot(-I, N)
  cos1 = -1 * Dot(*I, *N);
  if (cos1 < 0) {
    cos1 *= -1;
    eta = 1/ior;
    n.x = -N->x;
    n.y = -N->y;
    n.z = -N->z;
  } else {
    eta = ior;
    n = *N;
  }

  radicand = 1 - eta*eta * (1 - cos1*cos1);

  if (radicand < 0.) {
    // total internal reflection
    n.x = -N->x;
    n.y = -N->y;
    n.z = -N->z;
    SlReflect(I, N, T);
    return;
  }

  ncoeff = eta * cos1 - sqrt(radicand);

  T->x = eta * I->x + ncoeff * n.x;
  T->y = eta * I->y + ncoeff * n.y;
  T->z = eta * I->z + ncoeff * n.z;
}

int SlTrace(const TraceContext *cxt,
    const Vector *ray_orig, const Vector *ray_dir,
    double ray_tmin, double ray_tmax, Color4 *out_rgba, double *t_hit)
{
  Ray ray;
  Color4 surface_color;
  Color4 volume_color;
  int hit_surface = 0;
  int hit_volume = 0;

  out_rgba->r = 0;
  out_rgba->g = 0;
  out_rgba->b = 0;
  out_rgba->a = 0;
  if (has_reached_bounce_limit(cxt)) {
    return 0;
  }

  setup_ray(ray_orig, ray_dir, ray_tmin, ray_tmax, &ray);

  hit_surface = trace_surface(cxt, ray, &surface_color, t_hit);

  if (shadow_ray_has_reached_opcity_limit(cxt, surface_color.a)) {
    *out_rgba = surface_color;
    return 1;
  }

  if (hit_surface) {
    ray.tmax = *t_hit;
  }

  hit_volume = raymarch_volume(cxt, &ray, &volume_color);

  out_rgba->r = volume_color.r + surface_color.r * (1 - volume_color.a);
  out_rgba->g = volume_color.g + surface_color.g * (1 - volume_color.a);
  out_rgba->b = volume_color.b + surface_color.b * (1 - volume_color.a);
  out_rgba->a = volume_color.a + surface_color.a * (1 - volume_color.a);

  return hit_surface || hit_volume;
}

int SlSurfaceRayIntersect(const TraceContext *cxt,
    const Vector *ray_orig, const Vector *ray_dir,
    double ray_tmin, double ray_tmax,
    Vector *P_hit, Vector *N_hit, double *t_hit)
{
  const Accelerator *acc = NULL;
  Intersection isect;
  Ray ray;
  int hit = 0;

  setup_ray(ray_orig, ray_dir, ray_tmin, ray_tmax, &ray);
  acc = cxt->trace_target->GetSurfaceAccelerator();
  hit = acc->Intersect(ray, cxt->time, &isect);

  if (hit) {
    *P_hit = isect.P;
    *N_hit = isect.N;
    *t_hit = isect.t_hit;
  }

  return hit;
}

TraceContext SlCameraContext(const ObjectGroup *target)
{
  TraceContext cxt;

  cxt.ray_context = CXT_CAMERA_RAY;
  cxt.diffuse_depth = 0;
  cxt.reflect_depth = 0;
  cxt.refract_depth = 0;
  cxt.max_diffuse_depth = 5;
  cxt.max_reflect_depth = 5;
  cxt.max_refract_depth = 5;
  cxt.cast_shadow = 1;
  cxt.trace_target = target;

  cxt.time = 0;

  cxt.opacity_threshold = .995f;
  cxt.raymarch_step = .05;
  cxt.raymarch_shadow_step = .05;
  cxt.raymarch_diffuse_step = .05;
  cxt.raymarch_reflect_step = .05;
  cxt.raymarch_refract_step = .05;

  return cxt;
}

TraceContext SlDiffuseContext(const TraceContext *cxt,
    const ObjectInstance *obj)
{
  TraceContext diff_cxt = *cxt;

  diff_cxt.diffuse_depth++;
  diff_cxt.ray_context = CXT_DIFFUSE_RAY;
  diff_cxt.trace_target = obj->GetReflectTarget();

  return diff_cxt;
}

TraceContext SlReflectContext(const TraceContext *cxt,
    const ObjectInstance *obj)
{
  TraceContext refl_cxt = *cxt;

  refl_cxt.reflect_depth++;
  refl_cxt.ray_context = CXT_REFLECT_RAY;
  refl_cxt.trace_target = obj->GetReflectTarget();

  return refl_cxt;
}

TraceContext SlRefractContext(const TraceContext *cxt,
    const ObjectInstance *obj)
{
  TraceContext refr_cxt = *cxt;

  refr_cxt.refract_depth++;
  refr_cxt.ray_context = CXT_REFRACT_RAY;
  refr_cxt.trace_target = obj->GetRefractTarget();

  return refr_cxt;
}

TraceContext SlShadowContext(const TraceContext *cxt,
    const ObjectInstance *obj)
{
  TraceContext shad_cxt = *cxt;

  shad_cxt.ray_context = CXT_SHADOW_RAY;
  // turn off the secondary trace on occluding objects
  shad_cxt.max_diffuse_depth = 0;
  shad_cxt.max_reflect_depth = 0;
  shad_cxt.max_refract_depth = 0;
  shad_cxt.trace_target = obj->GetShadowTarget();

  return shad_cxt;
}

TraceContext SlSelfHitContext(const TraceContext *cxt,
    const ObjectInstance *obj)
{
  TraceContext self_cxt = *cxt;

  self_cxt.trace_target = obj->GetSelfHitTarget();

  return self_cxt;
}

int SlGetLightCount(const SurfaceInput *in)
{
  return in->shaded_object->GetLightCount();
}

int SlIlluminance(const TraceContext *cxt, const LightSample *sample,
    const Vector *Ps, const Vector *axis, double angle,
    const SurfaceInput *in, LightOutput *out)
{
  double cosangle = 0.;
  Vector nml_axis;
  Color light_color;

  out->Cl.r = 0;
  out->Cl.g = 0;
  out->Cl.b = 0;

  out->Ln.x = sample->P.x - Ps->x;
  out->Ln.y = sample->P.y - Ps->y;
  out->Ln.z = sample->P.z - Ps->z;

  out->distance = Length(out->Ln);
  if (out->distance > 0) {
    const double inv_dist = 1. / out->distance;
    out->Ln.x *= inv_dist;
    out->Ln.y *= inv_dist;
    out->Ln.z *= inv_dist;
  }

  nml_axis = *axis;
  nml_axis = Normalize(nml_axis);
  cosangle = Dot(nml_axis, out->Ln);
  if (cosangle < cos(angle)) {
    return 0;
  }

  light_color = sample->light->Illuminate(*sample, *Ps);
  if (light_color.r < .0001 &&
    light_color.g < .0001 &&
    light_color.b < .0001) {
    return 0;
  }

  if (cxt->ray_context == CXT_SHADOW_RAY) {
    return 0;
  }

  if (cxt->cast_shadow) {
    TraceContext shad_cxt;
    Color4 C_occl;
    double t_hit = FLT_MAX;
    int hit = 0;

    shad_cxt = SlShadowContext(cxt, in->shaded_object);
    hit = SlTrace(&shad_cxt, Ps, &out->Ln, .0001, out->distance, &C_occl, &t_hit);

    if (hit) {
      // return 0;
      // TODO handle light_color for shadow ray
      const float alpha_complement = 1 - C_occl.a;
      light_color.r *= alpha_complement;
      light_color.g *= alpha_complement;
      light_color.b *= alpha_complement;
    }
  }

  out->Cl = light_color;
  return 1;
}

  // TODO temp solution compute before rendering
int SlGetLightSampleCount(const SurfaceInput *in)
{
  const Light **lights = in->shaded_object->GetLightList();
  const int nlights = SlGetLightCount(in);
  int nsamples = 0;
  int i;

  if (lights == NULL) {
    return 0;
  }

  for (i = 0; i < nlights; i++) {
    nsamples += lights[i]->GetSampleCount();
  }

  return nsamples;
}

LightSample *SlNewLightSamples(const SurfaceInput *in)
{
  const Light **lights = in->shaded_object->GetLightList();
  const int nlights = SlGetLightCount(in);
  const int nsamples = SlGetLightSampleCount(in);
  int i;

  LightSample *samples = NULL;
  LightSample *sample = NULL;

  if (nsamples == 0) {
    // TODO handling
    return NULL;
  }

  samples = new LightSample[nsamples];
  sample = samples;
  for (i = 0; i < nlights; i++) {
    const int nsmp = lights[i]->GetSampleCount();
    lights[i]->GetSamples(sample, nsmp);
    sample += nsmp;
  }

  return samples;
}

void SlFreeLightSamples(LightSample * samples)
{
  if (samples == NULL)
    return;
  delete [] samples;
}

#define MUL(a,val) do { \
  (a)->x *= (val); \
  (a)->y *= (val); \
  (a)->z *= (val); \
  } while(0)
void SlBumpMapping(const Texture *bump_map,
    const Vector *dPdu, const Vector *dPdv,
    const TexCoord *texcoord, double amplitude,
    const Vector *N, Vector *N_bump)
{
  Color4 C_tex0(0, 0, 0, 1);
  Color4 C_tex1(0, 0, 0, 1);
  Vector N_dPdu;
  Vector N_dPdv;
  float Bu, Bv;
  float du, dv;
  float val0, val1;
  const int xres = bump_map->GetWidth();
  const int yres = bump_map->GetHeight();
  
  if (xres == 0 || yres == 0) {
    return;
  }

  du = 1. / xres;
  dv = 1. / yres;

  // Bu = B(u - du, v) - B(v + du, v) / (2 * du)
  C_tex0 = bump_map->Lookup(texcoord->u - du, texcoord->v);
  C_tex1 = bump_map->Lookup(texcoord->u + du, texcoord->v);
  val0 = Luminance4(C_tex0);
  val1 = Luminance4(C_tex1);
  Bu = (val0 - val1) / (2 * du);

  // Bv = B(u, v - dv) - B(v, v + dv) / (2 * dv)
  C_tex0 = bump_map->Lookup(texcoord->u, texcoord->v - dv);
  C_tex1 = bump_map->Lookup(texcoord->u, texcoord->v + dv);
  val0 = Luminance4(C_tex0);
  val1 = Luminance4(C_tex1);
  Bv = (val0 - val1) / (2 * dv);

  // N ~= N + Bv(N x Pu) + Bu(N x Pv)
  N_dPdu = Cross(*N, *dPdu);
  N_dPdv = Cross(*N, *dPdv);
  MUL(&N_dPdu, du);
  MUL(&N_dPdv, du);
  N_bump->x = N->x + amplitude * (Bv * N_dPdu.x - Bu * N_dPdv.x);
  N_bump->y = N->y + amplitude * (Bv * N_dPdu.y - Bu * N_dPdv.y);
  N_bump->z = N->z + amplitude * (Bv * N_dPdu.z - Bu * N_dPdv.z);

  *N_bump = Normalize(*N_bump);
}
#undef MUL

static int has_reached_bounce_limit(const TraceContext *cxt)
{
  int current_depth = 0;
  int max_depth = 0;

  switch (cxt->ray_context) {
  case CXT_CAMERA_RAY:
    current_depth = 0;
    max_depth = 1;
    break;
  case CXT_SHADOW_RAY:
    current_depth = 0;
    max_depth = 1;
    break;
  case CXT_DIFFUSE_RAY:
    current_depth = cxt->diffuse_depth;
    max_depth = cxt->max_diffuse_depth;
    break;
  case CXT_REFLECT_RAY:
    current_depth = cxt->reflect_depth;
    max_depth = cxt->max_reflect_depth;
    break;
  case CXT_REFRACT_RAY:
    current_depth = cxt->refract_depth;
    max_depth = cxt->max_refract_depth;
    break;
  default:
    assert(!"invalid ray type");
    break;
  }

  return current_depth > max_depth;
}

static void setup_ray(const Vector *ray_orig, const Vector *ray_dir,
    double ray_tmin, double ray_tmax,
    Ray *ray)
{
  ray->orig = *ray_orig;
  ray->dir = *ray_dir;
  ray->tmin = ray_tmin;
  ray->tmax = ray_tmax;
}

static void setup_surface_input(
    const Intersection *isect,
    const Ray *ray,
    SurfaceInput *in)
{
  in->shaded_object = isect->object;
  in->P  = isect->P;
  in->N  = isect->N;
  in->Cd = isect->Cd;
  in->uv = isect->uv;
  in->I  = ray->dir;

  in->dPdu = isect->dPdu;
  in->dPdv = isect->dPdv;
}

static int trace_surface(const TraceContext *cxt, const Ray &ray,
    Color4 *out_rgba, double *t_hit)
{
  const Accelerator *acc = NULL;
  Intersection isect;
  int hit = 0;

  out_rgba->r = 0;
  out_rgba->g = 0;
  out_rgba->b = 0;
  out_rgba->a = 0;
  acc = cxt->trace_target->GetSurfaceAccelerator();
  hit = acc->Intersect(ray, cxt->time, &isect);

  // TODO handle shadow ray for surface geometry
#if 0
  if (cxt->ray_context == CXT_SHADOW_RAY) {
    return hit;
  }
#endif

  if (hit) {
    SurfaceInput in;
    SurfaceOutput out;

    setup_surface_input(&isect, &ray, &in);

    const Shader *shader = isect.GetShader();
    if (shader != NULL) {
      shader->Evaluate(*cxt, in, &out);
    } else {
      out.Cs = NO_SHADER_COLOR;
      out.Os = 1;
    }

    out.Os = Clamp(out.Os, 0, 1);
    out_rgba->r = out.Cs.r;
    out_rgba->g = out.Cs.g;
    out_rgba->b = out.Cs.b;
    out_rgba->a = out.Os;

    *t_hit = isect.t_hit;
  }

  return hit;
}

static int raymarch_volume(const TraceContext *cxt, const Ray *ray,
    Color4 *out_rgba)
{
  const VolumeAccelerator *acc = NULL;
  IntervalList intervals;
  int hit = 0;

  out_rgba->r = 0;
  out_rgba->g = 0;
  out_rgba->b = 0;
  out_rgba->a = 0;

  acc = cxt->trace_target->GetVolumeAccelerator();
  hit = VolumeAccIntersect(acc, cxt->time, ray, &intervals);

  if (!hit) {
    return 0;
  }

  {
    Vector P;
    Vector ray_delta;
    double t = 0, t_start = 0, t_delta = 0, t_limit = 0;
    const float opacity_threshold = cxt->opacity_threshold;

    // t properties
    switch (cxt->ray_context) {
    case CXT_CAMERA_RAY:
      t_delta = cxt->raymarch_step;
      break;
    case CXT_SHADOW_RAY:
      t_delta = cxt->raymarch_shadow_step;
      break;
    case CXT_DIFFUSE_RAY:
      t_delta = cxt->raymarch_diffuse_step;
      break;
    case CXT_REFLECT_RAY:
      t_delta = cxt->raymarch_reflect_step;
      break;
    case CXT_REFRACT_RAY:
      t_delta = cxt->raymarch_refract_step;
      break;
    default:
      t_delta = cxt->raymarch_step;
      break;
    }
    t_limit = intervals.GetMaxT();
    t_limit = Min(t_limit, ray->tmax);

    t_start = intervals.GetMinT();
    if (t_start < 0) {
      t_start = t_delta;
    }
    else {
      t_start = t_start - fmod(t_start, t_delta) + t_delta;
    }

    P = RayPointAt(*ray, t_start);
    ray_delta.x = t_delta * ray->dir.x;
    ray_delta.y = t_delta * ray->dir.y;
    ray_delta.z = t_delta * ray->dir.z;
    t = t_start;

    // raymarch
    while (t <= t_limit && out_rgba->a < opacity_threshold) {
      const Interval *interval = intervals.GetHead();
      Color color;
      float opacity = 0;

      // loop over volume candidates at this sample point
      for (; interval != NULL; interval = interval->next) {
        VolumeSample sample;
        interval->object->GetVolumeSample(P, cxt->time, &sample);

        // merge volume with max density
        opacity = Max(opacity, t_delta * sample.density);

        if (cxt->ray_context != CXT_SHADOW_RAY) {
          SurfaceInput in;
          SurfaceOutput out;

          in.shaded_object = interval->object;
          in.P = P;
          in.N = Vector(0, 0, 0);

          // TODO shading group
          const Shader *shader = interval->object->GetShader(0);
          if (shader != NULL) {
            shader->Evaluate(*cxt, in, &out);
          } else {
            out.Cs = NO_SHADER_COLOR;
            out.Os = 1;
          }

          color.r = out.Cs.r * opacity;
          color.g = out.Cs.g * opacity;
          color.b = out.Cs.b * opacity;
        }
      }

      // composite color
      out_rgba->r = out_rgba->r + color.r * (1-out_rgba->a);
      out_rgba->g = out_rgba->g + color.g * (1-out_rgba->a);
      out_rgba->b = out_rgba->b + color.b * (1-out_rgba->a);
      out_rgba->a = out_rgba->a + Clamp(opacity, 0, 1) * (1-out_rgba->a);

      // advance sample point
      P.x += ray_delta.x;
      P.y += ray_delta.y;
      P.z += ray_delta.z;
      t += t_delta;
    }
    if (out_rgba->a >= opacity_threshold) {
      out_rgba->a = 1;
    }
  }
  out_rgba->a = Clamp(out_rgba->a, 0, 1);

  return hit;
}

static int shadow_ray_has_reached_opcity_limit(const TraceContext *cxt, float opac)
{
  if (cxt->ray_context == CXT_SHADOW_RAY && opac > cxt->opacity_threshold) {
    return 1;
  } else {
    return 0;
  }
}

} // namespace xxx
