// Copyright 2011, Tokyo Instiute of Technology.
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#ifndef PHYSIS_PHYSIS_USER_H_
#define PHYSIS_PHYSIS_USER_H_

#include "physis/physis_common.h"

#ifdef __cplusplus
extern "C" {
#endif
  
  /*
    Functions returning values of grid element type, such as get, cannot
    be represented as standard C or C++ since return-type polymorphism
    is not possible. Such functions need to be declared in the context
    of the grid type declcaration.
  */
#define DeclareGrid1D(name, type)                               \
  struct __PSGrid1D##name {                                     \
    type _type_indicator;                                       \
    void (*set)(PSIndex, type);                                 \
    type (*get)(PSIndex);                                       \
    type (*get_periodic)(PSIndex);                              \
    type (*emit)(type);                                         \
    type (*emitDirichlet)(type);                                \
    type (*emitNeumann)(type, int);                             \
    type (*reduce)(void *dom, type (*kernel)(type, type));      \
  };                                                            \
  typedef struct __PSGrid1D##name *PSGrid1D##name;              \
  extern PSGrid1D##name PSGrid1D##name##New(PSIndex, ...);

#define DeclareGrid2D(name, type)                                       \
  struct __PSGrid2D##name {                                             \
    type _type_indicator;                                               \
    void (*set)(PSIndex, PSIndex, type);                                \
    type (*get)(PSIndex, PSIndex);                                      \
    type (*get_periodic)(PSIndex, PSIndex);                             \
    type (*emit)(type);                                                 \
    type (*emitDirichlet)(type);                                        \
    type (*emitNeumann)(type, int);                                     \
    type (*reduce)(void *dom, type (*kernel)(type, type));              \
  };                                                                    \
  typedef struct __PSGrid2D##name *PSGrid2D##name;                      \
  extern PSGrid2D##name PSGrid2D##name##New(PSIndex, PSIndex, ...);

#define DeclareGrid3D(name, type)                                       \
  struct __PSGrid3D##name {                                             \
    type _type_indicator;                                               \
    void (*set)(PSIndex, PSIndex, PSIndex, type);                       \
    type (*get)(PSIndex, PSIndex, PSIndex);                             \
    type (*get_periodic)(PSIndex, PSIndex, PSIndex);                    \
    type (*emit)(type);                                                 \
    type (*emitDirichlet)(type);                                        \
    type (*emitNeumann)(type, int);                                     \
    type (*reduce)(void *dom, type (*kernel)(type, type));              \
  };                                                                    \
  typedef struct __PSGrid3D##name *PSGrid3D##name;                      \
  extern PSGrid3D##name PSGrid3D##name##New(PSIndex, PSIndex, PSIndex, ...);
  
  DeclareGrid1D(Float, float);
  DeclareGrid1D(Double, double);
  DeclareGrid1D(Int, int);
  DeclareGrid1D(Long, long);
  DeclareGrid2D(Float, float);
  DeclareGrid2D(Double, double);
  DeclareGrid2D(Int, int);
  DeclareGrid2D(Long, long);
  DeclareGrid3D(Float, float);
  DeclareGrid3D(Double, double);
  DeclareGrid3D(Int, int);
  DeclareGrid3D(Long, long);

  //#undef DeclareGrid1D  
  //#undef DeclareGrid2D  
  //#undef DeclareGrid3D

  extern void *__PSGridEmitUtype(char *s, ...);
  
#define PSGridGet(g, ...) g->get(__VA_ARGS__)
#define PSGridGetPeriodic(g, ...) g->get_periodic(__VA_ARGS__)  
#define PSGridSet(g, ...) g->set(__VA_ARGS__)  
#define PSGridEmit(g, v) g->emit(v)
#define PSGridEmitUtype(g, v) (*(typeof(v)*)(__PSGridEmitUtype(#g, v)))  
#define PSGridEmitDirichlet(g, v) g->emitDirichlet(v)  
#define PSGridEmitNeumann(g, v, grad) g->emitNeumann(v, grad)
  //#define grid_map(d, k, g, ...) g.map(&d, #(void*)k,###__VA_ARGS__)
  //#define grid_map(d, k, ...) _grid_map((void*)&d, (void*)k,
  //###__VA_ARGS__)
  //#define grid_map(d, k,...) d.map((void*)k,__VA_ARGS__)
  //#define grid_map2(d, k,...) stencil_run(stencil_new(d, k,__VA_ARGS__))
  //#define grid_map2(k,...) _grid_map((void*)k,__VA_ARGS__)
  //#define grid_copyin(g, v) g.copyin(v)

  extern PSIndex PSGridDim(void *g, int d);
  typedef int PSStencil;
  // explicit cast to void* is necessary to suppress compiler type
  // mismatch warning in C++
#define PSStencilMap(k, ...) __PSStencilMap((void*)k, __VA_ARGS__)
  extern PSStencil __PSStencilMap(void *, ...);
#define PSStencilMapRedBlack(k, ...) __PSStencilMapRedBlack((void*)k, __VA_ARGS__)
  extern PSStencil __PSStencilMapRedBlack(void *, ...);
#define PSStencilMapRed(k, ...) __PSStencilMapRed((void*)k, __VA_ARGS__)  
  extern PSStencil __PSStencilMapRed(void *, ...);
#define PSStencilMapBlack(k, ...) __PSStencilMapBlack((void*)k, __VA_ARGS__)  
  extern PSStencil __PSStencilMapBlack(void *, ...);    
  extern void PSStencilRun(PSStencil, ...);

  extern void PSReduce(void *v, ...);

#ifdef __cplusplus
}
#endif

#endif /* PHYSIS_PHYSIS_USER_H_ */
