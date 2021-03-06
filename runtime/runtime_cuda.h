// Copyright 2011-2012, RIKEN AICS.
// All rights reserved.
//
// This file is distributed under the BSD license. See LICENSE.txt for
// details.

#ifndef PHYSIS_RUNTIME_RUNTIME_CUDA_H_
#define PHYSIS_RUNTIME_RUNTIME_CUDA_H_

#include "runtime/runtime_common.h"
#include "runtime/runtime.h"

namespace physis {
namespace runtime {

class RuntimeCUDA: public Runtime {
 public:
  RuntimeCUDA();
  virtual ~RuntimeCUDA();
};

} // namespace runtime
} // namespace physis

#endif /* PHYSIS_RUNTIME_RUNTIME_CUDA_H_ */

