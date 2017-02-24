// Copyright (c) 2011-2017 Hiroshi Tsubokawa
// See LICENSE and README

#include "fj_compatibility.h"
#include "fj_progress.h"
#include "fj_triangle.h"
#include "fj_curve_io.h"
#include "fj_numeric.h"
#include "fj_mesh_io.h"
#include "fj_random.h"
#include "fj_vector.h"
#include "fj_color.h"
#include "fj_noise.h"
#include "fj_mesh.h"
#include "fj_box.h"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cfloat>

using namespace fj;

static const char USAGE[] =
"Usage: curvegen [options] inputfile(*.mesh) outputfile(*.crv)\n"
"Options:\n"
"  --help         Display this information\n"
"  --hair         Generates hair with velocity attribute\n"
"\n";

static int gen_hair(int argc, const char **argv);

int main(int argc, const char **argv)
{
  Progress progress;

  CurveOutput *out;
  const char *meshfile;
  const char *curvefile;

  int nfaces = 0;

  int total_ncurves;
  int total_ncps;

  int curve_id;
  int cp_id;
  int i;

  if (argc == 2 && strcmp(argv[1], "--help") == 0) {
    printf("%s", USAGE);
    return 0;
  }

  if (argc == 4 && strcmp(argv[1], "--hair") == 0) {
    return gen_hair(argc, argv);
  }

  if (argc != 3) {
    fprintf(stderr, "error: invalid number of arguments.\n");
    fprintf(stderr, "%s", USAGE);
    return -1;
  }
  meshfile = argv[1];
  curvefile = argv[2];

  Mesh mesh;

  if (MshLoadFile(&mesh, meshfile)) {
    const char *err_msg = NULL;

    switch (MshGetErrorNo()) {
    case MSH_ERR_NONE:
      err_msg = "";
      break;
    case MSH_ERR_FILE_NOT_EXIST:
      err_msg = "mesh file not found";
      break;
    case MSH_ERR_BAD_MAGIC_NUMBER:
      err_msg = "invalid magic number";
      break;
    case MSH_ERR_BAD_FILE_VERSION:
      err_msg = "invalid file format version";
      break;
    case MSH_ERR_LONG_ATTRIB_NAME:
      err_msg = "too long attribute name was detected";
      break;
    case MSH_ERR_NO_MEMORY:
      err_msg = "no memory to allocate";
      break;
    default:
      err_msg = "";
      break;
    }
    fprintf(stderr, "error: %s: %s\n", err_msg, meshfile);
    return -1;
  }

  nfaces = mesh.GetFaceCount();
  printf("nfaces: %d\n", nfaces);

  // count total_ncurves
  std::vector<int> ncurves_on_face(nfaces);
  total_ncurves = 0;
  for (i = 0; i < nfaces; i++) {
    Vector P0;
    Vector P1;
    Vector P2;
    double area = 0;

    MshGetFacePointPosition(&mesh, i, &P0, &P1, &P2);
    area = TriComputeArea(P0, P1, P2);

    ncurves_on_face[i] = 100000 * area;
    total_ncurves += ncurves_on_face[i];
  }
  printf("total_ncurves: %d\n", total_ncurves);

  total_ncps = 4 * total_ncurves;
  std::vector<Vector> P(total_ncps);
  std::vector<double> width(total_ncps);
  std::vector<Color>  Cd(total_ncps);
  std::vector<int>    indices(total_ncurves);

  std::vector<Vector> sourceP(total_ncurves);
  std::vector<Vector> sourceN(total_ncurves);

  printf("Computing curve's positions ...\n");
  progress.Start(total_ncurves);

  curve_id = 0;
  for (i = 0; i < nfaces; i++) {
    int j;
    Vector P0;
    Vector P1;
    Vector P2;
    Vector N0;
    Vector N1;
    Vector N2;

    MshGetFacePointPosition(&mesh, i, &P0, &P1, &P2);
    MshGetFacePointNormal(&mesh, i, &N0, &N1, &N2);

    for (j = 0; j < ncurves_on_face[i]; j++) {
      double gravity;
      double u, v, t;
      Vector *src_P;
      Vector *src_N;

      srand(12.34*i + 1232*j);
      u = (((double) rand()) / RAND_MAX);
      srand(21.43*i + 213*j);
      v = (1-u) * (((double) rand()) / RAND_MAX);

      src_P = &sourceP[curve_id];
      src_N = &sourceN[curve_id];

      t = 1-u-v;
      src_P->x = t * P0.x + u * P1.x + v * P2.x;
      src_P->y = t * P0.y + u * P1.y + v * P2.y;
      src_P->z = t * P0.z + u * P1.z + v * P2.z;

      src_N->x = t * N0.x + u * N1.x + v * N2.x;
      src_N->y = t * N0.y + u * N1.y + v * N2.y;
      src_N->z = t * N0.z + u * N1.z + v * N2.z;

      *src_N = Normalize(*src_N);

      srand(i+j);
      gravity = .5 + .5 * (((double) rand()) / RAND_MAX);
      src_N->y -= gravity;
      *src_N = Normalize(*src_N);

      curve_id++;

      progress.Increment();
    }
  }
  assert(curve_id == total_ncurves);
  progress.Done();

  printf("Generating curves ...\n");
  progress.Start(total_ncurves);

  cp_id = 0;
  curve_id = 0;
  for (i = 0; i < total_ncurves; i++) {
    int vtx;

    for (vtx = 0; vtx < 4; vtx++) {
      const double LENGTH = .02;
      Vector *dst_P;
      Vector *src_P;
      Vector *src_N;
      Vector noisevec;
      double noiseamp;
      Color *dst_Cd;

      srand(12*i + 49*vtx);
      if (vtx > 0) {
        noisevec.x = (((double) rand()) / RAND_MAX);
        noisevec.y = (((double) rand()) / RAND_MAX);
        noisevec.z = (((double) rand()) / RAND_MAX);
      }
      noiseamp = .75 * LENGTH;

      dst_P = &P[cp_id];
      src_P = &sourceP[curve_id];
      src_N = &sourceN[curve_id];

      dst_P->x = src_P->x + noiseamp * noisevec.x + vtx * LENGTH/3. * src_N->x;
      dst_P->y = src_P->y + noiseamp * noisevec.y + vtx * LENGTH/3. * src_N->y;
      dst_P->z = src_P->z + noiseamp * noisevec.z + vtx * LENGTH/3. * src_N->z;

      if (vtx == 0) {
        double *w = &width[cp_id];
        w[0] = .003;
        w[1] = .002;
        w[2] = .001;
        w[3] = .0001;
      }

      dst_Cd = &Cd[cp_id];
      {
        double amp = 1;
        double C_noise = 0;
        Color C_dark(.8, .5, .3);
        Color C_light(.9, .88, .85);
        Vector freq(3, 3, 3);
        Vector offset(0, 1, 0);
        Vector src_Q;

        src_Q.x = src_P->x * freq.x + offset.x;
        src_Q.y = src_P->y * freq.y + offset.y;
        src_Q.z = src_P->z * freq.z + offset.z;

        C_noise = amp * PerlinNoise(src_Q, 2, .5, 2);
        C_noise = SmoothStep(.55, .75, C_noise);
        *dst_Cd = Lerp(C_dark, C_light, C_noise);
      }

      cp_id++;
    }
    indices[curve_id] = 4*i;
    curve_id++;

    progress.Increment();
  }
  assert(cp_id == total_ncps);
  progress.Done();

  out = CrvOpenOutputFile(curvefile);
  if (out == NULL) {
    fprintf(stderr, "error: %s: %s\n", CrvGetErrorMessage(CrvGetErrorNo()), curvefile);
    return -1;
  }

  // setup CurveOutput
  out->nverts = total_ncps;
  out->nvert_attrs = 2;
  out->P = &P[0];
  out->width = &width[0];
  out->Cd = &Cd[0];
  out->uv = NULL;
  out->ncurves = total_ncurves;
  out->ncurve_attrs = 1;
  out->indices = &indices[0];

  CrvWriteFile(out);
  CrvCloseOutputFile(out);

  return 0;
}

static int gen_hair(int argc, const char **argv)
{
  const int N_CURVES_PER_HAIR = 5;

  Progress progress;

  CurveOutput *out;
  const char *meshfile;
  const char *curvefile;

  int nfaces = 0;

  XorShift rng;
  double ymin, ymax;
  double zmin, zmax;

  int total_ncurves;
  int total_ncps;

  int strand_id;
  int curve_id;
  int cp_id;
  int i;

  meshfile = argv[2];
  curvefile = argv[3];

  Mesh mesh;

  if (MshLoadFile(&mesh, meshfile)) {
    const char *err_msg = NULL;

    switch (MshGetErrorNo()) {
    case MSH_ERR_NONE:
      err_msg = "";
      break;
    case MSH_ERR_FILE_NOT_EXIST:
      err_msg = "mesh file not found";
      break;
    case MSH_ERR_BAD_MAGIC_NUMBER:
      err_msg = "invalid magic number";
      break;
    case MSH_ERR_BAD_FILE_VERSION:
      err_msg = "invalid file format version";
      break;
    case MSH_ERR_LONG_ATTRIB_NAME:
      err_msg = "too long attribute name was detected";
      break;
    case MSH_ERR_NO_MEMORY:
      err_msg = "no memory to allocate";
      break;
    default:
      err_msg = "";
      break;
    }
    fprintf(stderr, "error: %s: %s\n", err_msg, meshfile);
    return -1;
  }

  {
    const int64_t N = mesh.GetPointCount();
    Box bounds;
    bounds.ReverseInfinite();

    int p;
    for (p = 0; p < N; p++) {
      const Vector pt = mesh.GetPointPosition(p);
      bounds.AddPoint(pt);
    }
    ymin = bounds.min.y;
    ymax = bounds.max.y;
    zmin = bounds.min.z;
    zmax = bounds.max.z;
  }

  nfaces = mesh.GetFaceCount();
  printf("nfaces: %d\n", nfaces);

  // count total_ncurves
  std::vector<int> ncurves_on_face(nfaces);
  total_ncurves = 0;
  for (i = 0; i < nfaces; i++) {
    Vector P0;
    Vector P1;
    Vector P2;
    double area = 0;

    double ycenter, ynml;
    double zcenter, znml;

    MshGetFacePointPosition(&mesh, i, &P0, &P1, &P2);
    area = TriComputeArea(P0, P1, P2);

    ycenter = (P0.y + P1.y + P2.y) / 3.;
    ynml = (ycenter - ymin) / (ymax - ymin);
    zcenter = (P0.z + P1.z + P2.z) / 3.;
    znml = (zcenter - zmin) / (zmax - zmin);

    ncurves_on_face[i] = 100000 * area;

    if (ynml < .5 || znml > .78) {
      ncurves_on_face[i] = 0;
    }

    total_ncurves += ncurves_on_face[i] * N_CURVES_PER_HAIR;
  }
  printf("total_ncurves: %d\n", total_ncurves);

  total_ncps = 4 * total_ncurves;
  std::vector<Vector> P(total_ncps);
  std::vector<double> width(total_ncps);
  std::vector<Color>  Cd(total_ncps);
  std::vector<Vector> velocity(total_ncps);
  std::vector<int>    indices(total_ncurves);

  printf("Computing curve's positions ...\n");
  progress.Start(total_ncurves / N_CURVES_PER_HAIR);

  strand_id = 0;
  curve_id = 0;
  cp_id = 0;
  for (i = 0; i < nfaces; i++) {
    int j;
    Vector P0;
    Vector P1;
    Vector P2;
    Vector N0;
    Vector N1;
    Vector N2;

    MshGetFacePointPosition(&mesh, i, &P0, &P1, &P2);
    MshGetFacePointNormal(&mesh, i, &N0, &N1, &N2);

    for (j = 0; j < ncurves_on_face[i]; j++) {
      double u, v, t;
      Vector src_P;
      Vector src_N;
      int k;

      u = rng.NextFloat01();
      v = (1-u) * rng.NextFloat01();

      t = 1-u-v;
      src_P.x = t * P0.x + u * P1.x + v * P2.x;
      src_P.y = t * P0.y + u * P1.y + v * P2.y;
      src_P.z = t * P0.z + u * P1.z + v * P2.z;

      src_N.x = t * N0.x + u * N1.x + v * N2.x;
      src_N.y = t * N0.y + u * N1.y + v * N2.y;
      src_N.z = t * N0.z + u * N1.z + v * N2.z;

      src_N = Normalize(src_N);

      src_N.y = Min(src_N.y, .1);
      if (src_N.x < .1 && src_N.z < .1) {
        src_N.x /= src_N.x;
        src_N.z /= src_N.z;
        src_N.x *= .5;
        src_N.z *= .5;
      }
      src_N = Normalize(src_N);

      {
        int vtx;
        Vector next_P = src_P;
        Vector next_N = src_N;

        for (k = 0; k < N_CURVES_PER_HAIR; k++) {
          // the first cp_id of curve
          indices[curve_id] = cp_id;

          for (vtx = 0; vtx < 4; vtx++) {
            const double w[4] = {1, .5, .2, .05};
            P[cp_id] = next_P;

            Cd[cp_id].r = .9;
            Cd[cp_id].g = .8;
            Cd[cp_id].b = .5;

            if (k == N_CURVES_PER_HAIR - 1) {
              width[cp_id] = .0005 * w[vtx];
            }
            else {
              width[cp_id] = .0005;
            }

            // update the 'next' next_P if not the last control point
            if (vtx != 3) {
              Vector curr_P = P[cp_id];
              Vector noise_vec;
              Vector Q;

              const double amp = .002 * .1;
              const double freq = 100;
              const double segment_len = .01;

              Q.x = curr_P.x * freq;
              Q.y = curr_P.y * 2;
              Q.z = curr_P.z * freq;
              noise_vec = PerlinNoise3d(Q, 2, .5, 2);

              next_P.x += segment_len * next_N.x + amp * noise_vec.x;
              next_P.y += segment_len * next_N.y;
              next_P.z += segment_len * next_N.z + amp * noise_vec.z;

              next_N.x = next_P.x - curr_P.x;
              next_N.y = next_P.y - curr_P.y;
              next_N.z = next_P.z - curr_P.z;
              next_N = Normalize(next_N);

              next_N.y += -.5;
              next_N = Normalize(next_N);
            }
            // compute velocity
            {
              Vector curr_P = P[cp_id];
              Vector curr_v;
              Vector noise_vec;
              Vector Q;

              const double amp = .01;
              const double freq = 1;

              double vmult = 1;

              Q.x = curr_P.x * freq;
              Q.y = curr_P.y * freq + 5;
              Q.z = curr_P.z * freq;
              noise_vec = PerlinNoise3d(Q, 2, .5, 2);

              vmult = SmoothStep(1, N_CURVES_PER_HAIR, k);

              curr_v.x = vmult * amp * noise_vec.x;
              curr_v.y = vmult * amp * noise_vec.y;
              curr_v.z = vmult * amp * noise_vec.z;

              velocity[cp_id] = curr_v;
            }

            cp_id++;
          }
          curve_id++;
        }
      }

      strand_id++;

      progress.Increment();
    }
  }
  assert(strand_id == total_ncurves / N_CURVES_PER_HAIR);
  progress.Done();

  out = CrvOpenOutputFile(curvefile);
  if (out == NULL) {
    fprintf(stderr, "error: %s: %s\n", CrvGetErrorMessage(CrvGetErrorNo()), curvefile);
    return -1;
  }

  // setup CurveOutput
  out->nverts = total_ncps;
  out->nvert_attrs = 2;
  out->P = &P[0];
  out->width = &width[0];
  out->Cd = &Cd[0];
  out->uv = NULL;
  out->velocity = &velocity[0];
  out->ncurves = total_ncurves;
  out->ncurve_attrs = 1;
  out->indices = &indices[0];

  CrvWriteFile(out);
  CrvCloseOutputFile(out);

  return 0;
}
