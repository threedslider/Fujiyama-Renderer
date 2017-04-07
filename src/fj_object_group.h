// Copyright (c) 2011-2017 Hiroshi Tsubokawa
// See LICENSE and README

#ifndef FJ_OBJECT_GROUP_H
#define FJ_OBJECT_GROUP_H

#include "fj_object_set.h"

namespace fj {

class ObjectInstance;
class VolumeAccelerator;
class Accelerator;

class ObjectGroup {
public:
  ObjectGroup();
  ~ObjectGroup();

  void AddObject(const ObjectInstance *obj);
  const Accelerator *GetSurfaceAccelerator() const;
  const VolumeAccelerator *GetVolumeAccelerator() const;

  void ComputeBounds();

private:
  ObjectSet surface_set_;
  ObjectSet volume_set_;

  Accelerator *surface_set_acc_;
  VolumeAccelerator *volume_set_acc_;
};

extern ObjectGroup *ObjGroupNew();
extern void ObjGroupFree(ObjectGroup *group);

} // namespace xxx

#endif // FJ_XXX_H
