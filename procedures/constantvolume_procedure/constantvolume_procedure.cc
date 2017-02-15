// Copyright (c) 2011-2016 Hiroshi Tsubokawa
// See LICENSE and README

#include "fj_procedure.h"
#include "fj_numeric.h"
#include "fj_vector.h"
#include "fj_volume.h"

using namespace fj;

class ConstantVolumeProcedure : Procedure {
public:
  ConstantVolumeProcedure() : volume(NULL), density(1.) {}
  virtual ~ConstantVolumeProcedure() {}

public:
  Volume *volume;
  float density;

private:
  virtual int run() const;
  const Property *get_property_list() const;
};

static void *MyCreateFunction(void);
static void MyDeleteFunction(void *self);
static const char MyPluginName[] = "ConstantVolumeProcedure";

static int set_volume(void *self, const PropertyValue *value);
static int set_density(void *self, const PropertyValue *value);

static int FillWithConstant(Volume *volume, float density);

static const Property MyPropertyList[] = {
  {PROP_VOLUME, "volume",  {0, 0, 0, 0}, set_volume},
  {PROP_SCALAR, "density", {1, 0, 0, 0}, set_density},
  {PROP_NONE,   NULL,      {0, 0, 0, 0}, NULL}
};

static const MetaInfo MyMetainfo[] = {
  {"help", "A constant volume procedure."},
  {"plugin_type", "Procedure"},
  {NULL, NULL}
};

extern "C" {
FJ_PLUGIN_API int Initialize(PluginInfo *info)
{
  return PlgSetupInfo(info,
      PLUGIN_API_VERSION,
      PROCEDURE_PLUGIN_TYPE,
      MyPluginName,
      MyCreateFunction,
      MyDeleteFunction,
      MyPropertyList,
      MyMetainfo);
}
} // extern "C"

static void *MyCreateFunction(void)
{
  ConstantVolumeProcedure *constvol = new ConstantVolumeProcedure();

  return constvol;
}

static void MyDeleteFunction(void *self)
{
  ConstantVolumeProcedure *constvol = (ConstantVolumeProcedure *) self;
  if (constvol == NULL)
    return;
  delete constvol;
}

int ConstantVolumeProcedure::run() const
{
  if (volume == NULL) {
    return -1;
  }

  const int err = FillWithConstant(volume, density);

  return err;
}

const Property *ConstantVolumeProcedure::get_property_list() const
{
  return MyPropertyList;
}

static int set_volume(void *self, const PropertyValue *value)
{
  ConstantVolumeProcedure *constvol = (ConstantVolumeProcedure *) self;

  if (value->volume == NULL)
    return -1;

  constvol->volume = value->volume;

  return 0;
}

static int set_density(void *self, const PropertyValue *value)
{
  ConstantVolumeProcedure *constvol = (ConstantVolumeProcedure *) self;

  constvol->density = Max(0, value->vector[0]);

  return 0;
}

static int FillWithConstant(Volume *volume, float density)
{
  int xres, yres, zres;
  volume->GetResolution(&xres, &yres, &zres);

  for (int k = 0; k < zres; k++) {
    for (int j = 0; j < yres; j++) {
      for (int i = 0; i < xres; i++) {
        volume->SetValue(i, j, k, density);
      }
    }
  }

  return 0;
}