// Copyright 2011, Tokyo Institute of Technology.
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#ifndef PHYSIS_PHYSIS_CUDA_H_
#define PHYSIS_PHYSIS_CUDA_H_

#include "physis/physis_common.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct {
    void *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    void *p1;
#endif    
    int dim[1];
  } __PSGridDev1D;

  typedef struct {
    float *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    float *p1;
#endif    
    int dim[1];    
  } __PSGridDev1DFloat;

  typedef struct {
    double *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    double *p1;
#endif        
    int dim[1];    
  } __PSGridDev1DDouble;

  // Note: int may not be enough for dim
  typedef struct {
    void *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    void *p1;
#endif        
    int dim[2];
  } __PSGridDev2D;

  typedef struct {
    float *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    float *p1;
#endif    
    int dim[2];
  } __PSGridDev2DFloat;

  typedef struct {
    double *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    double *p1;
#endif    
    int dim[2];
  } __PSGridDev2DDouble;

  typedef struct {
    void *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    void *p1;
#endif    
    int dim[3];
  } __PSGridDev3D;

  typedef struct {
    float *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    float *p1;
#endif    
    int dim[3];
  } __PSGridDev3DFloat;

  typedef struct {
    double *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    double *p1;
#endif    
    int dim[3];
  } __PSGridDev3DDouble;
  
  typedef struct {
    char *p0;
#ifdef AUTO_DOUBLE_BUFFERING    
    char *p1;
#endif
    PSVectorInt dim;    
    int elm_size;
    int num_dims;
    int64_t num_elms;
    void *dev;
  } __PSGrid;

#ifndef PHYSIS_USER
  typedef __PSGrid *PSGrid1DFloat;
  typedef __PSGrid *PSGrid2DFloat;
  typedef __PSGrid *PSGrid3DFloat;
  typedef __PSGrid *PSGrid1DDouble;
  typedef __PSGrid *PSGrid2DDouble;
  typedef __PSGrid *PSGrid3DDouble;
#define PSGridDim(p, d) (((__PSGrid *)(p))->dim[(d)])
#define __PSGridDimDev(p, d) ((p)->dim[d])
#else
  extern __PSGridDimDev(void *p, int);
#endif

  extern __PSGrid* __PSGridNew(int elm_size, int num_dims, PSVectorInt dim,
                               int double_buffering);
  extern void __PSGridSwap(__PSGrid *g);
  extern void __PSGridMirror(__PSGrid *g);
  extern int __PSGridGetID(__PSGrid *g);
  extern void __PSGridSet(__PSGrid *g, void *buf, ...);
  
  extern void __PSReduceGridFloat(void *buf, enum PSReduceOp op,
                                  __PSGrid *g);

  extern void __PSReduceGridDouble(void *buf, enum PSReduceOp op,
                                   __PSGrid *g);

  // CUDA Runtime APIs. Have signatures here to verify generated
  // ASTs.
#ifdef PHYSIS_USER
  typedef void* cudaStream_t;
  typedef int cudaError_t;
  extern cudaError_t cudaThreadSynchronize(void);
  extern cudaError_t cudaStreamSynchronize(cudaStream);
#endif

#ifdef __cplusplus
}
#endif
  

#endif /* PHYSIS_PHYSIS_CUDA_H_ */
