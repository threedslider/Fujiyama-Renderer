// Copyright (c) 2011-2017 Hiroshi Tsubokawa
// See LICENSE and README

#include "fj_adaptive_grid_sampler.h"
#include "fj_rectangle.h"
#include "fj_numeric.h"
#include "fj_random.h"

namespace fj {

AdaptiveGridSampler::AdaptiveGridSampler() :
  samples_(),

  nsamples_(1, 1),
  pixel_start_(0, 0),
  margin_(0, 0),
  npxlsmps_(1, 1),
  ndivision_(2, 2),

  curr_corner_(0)
{
}

AdaptiveGridSampler::~AdaptiveGridSampler()
{
}

void AdaptiveGridSampler::update_sample_counts()
{
  margin_ = count_samples_in_margin();
  npxlsmps_ = count_samples_in_pixel();
  ndivision_ = compute_num_pixel_division();
}

int AdaptiveGridSampler::generate_samples(const Rectangle &region)
{
  update_sample_counts();

  // allocate samples in region
  nsamples_ = count_samples_in_region(region);
  samples_.resize(nsamples_[0] * nsamples_[1]);
  pixel_start_ = region.min;

  XorShift rng; // random number generator
  XorShift rng_time; // for time sampling jitter

  const Int2 div = ndivision_;
  const Int2 res = GetResolution();
  const Real jitter = GetJitter();
  const Vector2 sample_time_range = GetSampleTimeRange();

  // uv delta (screen space uv. excludes margins)
  const Real udelta = 1./(div[0] * res[0]);
  const Real vdelta = 1./(div[1] * res[1]);

  // xy offset
  const int xoffset = (pixel_start_[0] - margin_[0]) * div[0];
  const int yoffset = (pixel_start_[1] - margin_[1]) * div[1];

  subd_flag_.clear();
  subd_flag_.resize(samples_.size(), -1);
  curr_corner_ = 0;
  int sample_id = 0;

  for (int y = 0; y < nsamples_[1]; y++) {
    for (int x = 0; x < nsamples_[0]; x++) {
      Sample &sample = samples_[sample_id];

      sample.uv[0] =     (x + xoffset) * udelta;
      sample.uv[1] = 1 - (y + yoffset) * vdelta;

      if (IsJittered()) {
        const Real u_jitter = rng.NextFloat01() * jitter;
        const Real v_jitter = rng.NextFloat01() * jitter;

        sample.uv[0] += udelta * (u_jitter - .5);
        sample.uv[1] += vdelta * (v_jitter - .5);
      }

      if (IsSamplingTime()) {
        const Real rnd = rng_time.NextFloat01();
        sample.time = Fit(rnd, 0, 1, sample_time_range[0], sample_time_range[1]);
      } else {
        sample.time = 0;
      }

      sample.data = Vector4();
      sample_id++;
    }
  }

  subd_stack_ = Stack();
  const Int2 tile_size = region.Size() + 2 * margin_;
  for (int y = 0; y < tile_size[1]; y++) {
    for (int x = 0; x < tile_size[0]; x++) {
      Rectangle r;
      r.min = Int2(x, y) * ndivision_;
      r.max = r.min + ndivision_;
      subd_stack_.push(r);
    }
  }
  return 0;
}

static void get_corner_points(const Rectangle &rect, Int2 corner[])
{
  /*
  corner[0] - corner[1]
     |           |
  corner[2] - corner[3]
  */
  corner[0] = Int2(rect.min[0], rect.min[1]);
  corner[1] = Int2(rect.max[0], rect.min[1]);
  corner[2] = Int2(rect.min[0], rect.max[1]);
  corner[3] = Int2(rect.max[0], rect.max[1]);
}

Sample *AdaptiveGridSampler::get_next_sample()
{
  while (!subd_stack_.empty()) {
    const Rectangle rect = subd_stack_.top();

    const Int2 size = rect.Size();
    const bool is_level_zero =
        size[0] == ndivision_[0] && 
        size[1] == ndivision_[1];

    // four corners are done?
    if (curr_corner_ == 4) {
      curr_corner_ = 0;
      subd_stack_.pop();
      subdivide_or_interpolate(rect);
      continue;
    }

    // get four corners
    Int2 corner[4];
    get_corner_points(rect, corner);

    // sample this corner
    const Int2 this_corner = corner[curr_corner_++];
    const int OFFSET = this_corner[1] * nsamples_[0] + this_corner[0];

    Sample *sample = &samples_[OFFSET];
    if (subd_flag_[OFFSET] < 0) {

      if (is_level_zero) {
        subd_flag_[OFFSET] = 0;
      } else {
        subd_flag_[OFFSET] = 1;
      }

      return sample;
    }
  }

#define STORE_SUBD_TO_ALPHA 0
  if (STORE_SUBD_TO_ALPHA) {
    for (std::size_t i = 0; i < samples_.size(); i++) {
      samples_[i].data[3] = subd_flag_[i];
    }
  }
#undef STORE_SUBD_TO_ALPHA

  return NULL;
}

void AdaptiveGridSampler::get_sampleset_in_pixel(std::vector<Sample> &pixelsamples,
    const Int2 &pixel_pos) const
{
  const Int2 div = ndivision_;

  const int XPIXEL_OFFSET = pixel_pos[0] - pixel_start_[0];
  const int YPIXEL_OFFSET = pixel_pos[1] - pixel_start_[1];

  const int XNSAMPLES = nsamples_[0];
  const int OFFSET =
    YPIXEL_OFFSET * div[1] * XNSAMPLES +
    XPIXEL_OFFSET * div[0];
  const Sample *src = &samples_[OFFSET];

  const Int2 NPXLSMPS = count_samples_in_pixel();
  const std::size_t SAMPLE_COUNT = static_cast<std::size_t>(NPXLSMPS[0] * NPXLSMPS[1]);
  if (pixelsamples.size() < SAMPLE_COUNT) {
    pixelsamples.resize(SAMPLE_COUNT);
  }

  std::vector<Sample> &dst = pixelsamples;

  for (int y = 0; y < npxlsmps_[1]; y++) {
    for (int x = 0; x < npxlsmps_[0]; x++) {
      dst[y * npxlsmps_[0] + x] = src[y * XNSAMPLES + x];
    }
  }
}

int AdaptiveGridSampler::get_sample_count() const
{
  return samples_.size();
}

Int2 AdaptiveGridSampler::count_samples_in_margin() const
{
  const Vector2 beyond_one = GetFilterWidth() - Vector2(1, 1);
  return Int2(
      static_cast<int>(Ceil(beyond_one[0])),
      static_cast<int>(Ceil(beyond_one[1])));
}

Int2 AdaptiveGridSampler::count_samples_in_pixel() const
{
  return ndivision_ * (Int2(1, 1) + 2 * margin_) + Int2(1, 1);
}

Int2 AdaptiveGridSampler::count_samples_in_region(const Rectangle &region) const
{
  return ndivision_ * (region.Size() + 2 * margin_) + Int2(1, 1);
}

Int2 AdaptiveGridSampler::compute_num_pixel_division() const
{
  const int nsubd = std::pow(2, GetMaxSubdivision());
  return Int2(nsubd, nsubd);
}

bool AdaptiveGridSampler::compare_corners(const Rectangle rect)
{
  const Int2 size = rect.Size();
  const bool reached_max_subd = size[0] < 2 || size[1] < 2;
  if (reached_max_subd) {
    return false;
  }

  Int2 corner[4];
  get_corner_points(rect, corner);

  Vector4 min, max;
  for (int i = 0; i < 4; i++) {
    const int OFFSET = corner[i][1] * nsamples_[0] + corner[i][0];
    const Vector4 &data = samples_[OFFSET].data;

    if (i == 0) {
      min = data;
      max = data;
    } else {
      min = Min(min, data);
      max = Max(max, data);
    }
  }

  bool beyond_threshold = false;
  for (int i = 0; i < 4; i++) {
    if (max[i] - min[i] > GetSubdivisionThreshold()) {
      beyond_threshold = true;
      break;
    }
  }

  return beyond_threshold;
}

void AdaptiveGridSampler::subdivide_rect(const Rectangle rect)
{
  Int2 corner[4];
  get_corner_points(rect, corner);

  /*
  corner[0] - new_pt[0] - corner[1]
     |           |           |
  new_pt[1] - new_pt[4] - new_pt[3]
     |           |           |
  corner[2] - new_pt[2] - corner[3]
  */
  Int2 new_pt[5];
  new_pt[0] = (corner[0] + corner[1]) / 2;
  new_pt[1] = (corner[0] + corner[2]) / 2;
  new_pt[2] = (corner[2] + corner[3]) / 2;
  new_pt[3] = (corner[1] + corner[3]) / 2;
  new_pt[4] = (new_pt[0] + new_pt[2]) / 2;

  Rectangle sub;
  sub.min = corner[0];
  sub.max = new_pt[4];
  subd_stack_.push(sub);

  sub.min = new_pt[0];
  sub.max = new_pt[3];
  subd_stack_.push(sub);

  sub.min = new_pt[1];
  sub.max = new_pt[2];
  subd_stack_.push(sub);

  sub.min = new_pt[4];
  sub.max = corner[3];
  subd_stack_.push(sub);
}

void AdaptiveGridSampler::interpolate_rect(const Rectangle rect)
{
  const int XMIN = rect.min[0], XMAX = rect.max[0];
  const int YMIN = rect.min[1], YMAX = rect.max[1];

  const Vector4 &data0 = samples_[YMIN * nsamples_[0] + XMIN].data;
  const Vector4 &data1 = samples_[YMIN * nsamples_[0] + XMAX].data;
  const Vector4 &data2 = samples_[YMAX * nsamples_[0] + XMIN].data;
  const Vector4 &data3 = samples_[YMAX * nsamples_[0] + XMAX].data;

  // using less than equal
  for (int y = YMIN; y <= YMAX; y++) {
    const Vector4 data02 = Lerp(data0, data2, 1.*(y - YMIN) / (YMAX-YMIN));
    const Vector4 data13 = Lerp(data1, data3, 1.*(y - YMIN) / (YMAX-YMIN));

    // using less than equal
    for (int x = XMIN; x <= XMAX; x++) {
      const int OFFSET = y * nsamples_[0] + x;
      Sample &sample = samples_[OFFSET];
      sample.data = Lerp(data02, data13, 1.*(x - XMIN) / (XMAX-XMIN));

      if (subd_flag_[OFFSET] < 0) {
        subd_flag_[OFFSET] = 0;
      }
    }
  }
}

bool AdaptiveGridSampler::subdivide_or_interpolate(const Rectangle rect)
{
  const bool need_subd = compare_corners(rect);

  if (need_subd) {
    subdivide_rect(rect);
  } else {
    interpolate_rect(rect);
  }

  return need_subd;
}

} // namespace xxx
