// Copyright (c) 2011-2017 Hiroshi Tsubokawa
// See LICENSE and README

#ifndef FJ_MATRIX_H
#define FJ_MATRIX_H

#include "fj_compatibility.h"
#include "fj_types.h"

namespace fj {

class FJ_API Matrix {
public:
  Matrix()
  {
    e[0]=1.;  e[1]=0.;  e[2]=0.;  e[3]=0.;
    e[4]=0.;  e[5]=1.;  e[6]=0.;  e[7]=0.;
    e[8]=0.;  e[9]=0.;  e[10]=1.; e[11]=0.;
    e[12]=0.; e[13]=0.; e[14]=0.; e[15]=1.;
  }
  Matrix(
    Real e00, Real e01, Real e02, Real e03,
    Real e10, Real e11, Real e12, Real e13,
    Real e20, Real e21, Real e22, Real e23,
    Real e30, Real e31, Real e32, Real e33)
  {
    e[0]=e00;  e[1]=e01;  e[2]=e02;  e[3]=e03;
    e[4]=e10;  e[5]=e11;  e[6]=e12;  e[7]=e13;
    e[8]=e20;  e[9]=e21;  e[10]=e22; e[11]=e23;
    e[12]=e30; e[13]=e31; e[14]=e32; e[15]=e33;
  }
  ~Matrix() {}

  // row-major
  Real e[16];
};

class Vector;
class Box;

FJ_API void MatIdentity(Matrix *dst);
FJ_API void MatSet(Matrix *dst,
    Real e00, Real e01, Real e02, Real e03,
    Real e10, Real e11, Real e12, Real e13,
    Real e20, Real e21, Real e22, Real e23,
    Real e30, Real e31, Real e32, Real e33);

FJ_API void MatTranslate(Matrix *dst, Real tx, Real ty, Real tz );
FJ_API void MatScale(Matrix *dst, Real sx, Real sy, Real sz );
FJ_API void MatRotateX(Matrix *dst, Real angle);
FJ_API void MatRotateY(Matrix *dst, Real angle);
FJ_API void MatRotateZ(Matrix *dst, Real angle);

FJ_API void MatMultiply(Matrix *dst, const Matrix &a, const Matrix &b);
FJ_API void MatInverse(Matrix *dst, const Matrix &a);

FJ_API void MatTransformPoint(const Matrix &m, Vector *point);
FJ_API void MatTransformVector(const Matrix &m, Vector *vector);
FJ_API void MatTransformBounds(const Matrix &m, Box *bounds);

FJ_API void MatPrint(const Matrix &m);

} // namespace xxx

#endif // FJ_XXX_H
