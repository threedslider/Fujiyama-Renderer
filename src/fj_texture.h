/*
Copyright (c) 2011-2014 Hiroshi Tsubokawa
See LICENSE and README
*/

#ifndef FJ_TEXTURE_H
#define FJ_TEXTURE_H

#include "fj_framebuffer.h"
#include <string>
#include <vector>

namespace fj {

class FrameBuffer;
class MipInput;
class Color4;

// Texture cache for each thread
class TextureCache {
public:
  TextureCache();
  ~TextureCache();

  int OpenMipmap(const std::string &filename);
  Color4 LookupTexture(float u, float v);

  int GetTextureWidth() const;
  int GetTextureHeight() const;
  bool IsOpen() const;

private:
  FrameBuffer fb_;
  MipInput *mip_;
  int last_xtile_;
  int last_ytile_;
  bool is_open_;
};

class Texture {
public:
  Texture();
  ~Texture();

  // Looks up a value of texture image.
  // (r, r, r, 1) will be returned when texture is grayscale.
  // (r, g, b, 1) will be returned when texture is rgb.
  // (r, g, b, a) will be returned when texture is rgba.
  Color4 Lookup(float u, float v) const;
  int LoadFile(const std::string &filename);

  int GetWidth() const;
  int GetHeight() const;

private:
  std::string filename_;
  std::vector<TextureCache> cache_list_;
};

extern Texture *TexNew(void);
extern void TexFree(Texture *tex);

} // namespace xxx

#endif // FJ_XXX_H
