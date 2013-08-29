// Copyright 2011-2013, RIKEN AICS.
// All rights reserved.
//
// This file is distributed under the BSD license. See LICENSE.txt for
// details.

#include "translator/cuda_hm_translator.h"

namespace pu = physis::util;
namespace sb = SageBuilder;
namespace si = SageInterface;

namespace physis {
namespace translator {

CUDAHMTranslator::CUDAHMTranslator(const Configuration &config):
    CUDATranslator(config) {
  target_specific_macro_ = "PHYSIS_CUDA_HM";  
}

} // namespace translator
} // namespace physis


