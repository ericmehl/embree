// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "intrinsics.h"
#include "mutex.h"

namespace embree
{
/* compiler memory barriers */
#if defined(__INTEL_COMPILER)
//#define __memory_barrier() __memory_barrier()
#elif defined(__GNUC__) || defined(__clang__)
#  define __memory_barrier() asm volatile("" ::: "memory")
#elif  defined(_MSC_VER)
#  define __memory_barrier() _ReadWriteBarrier()
#endif

  template <typename T>
    struct std__atomic : public std::atomic<T>
  {
    std__atomic () {}
      
    std__atomic (const T& a)
      : std::atomic<T>(a) {}

    std__atomic (const std__atomic<T>& a) {
      this->store(a.load());
    }

    std__atomic& operator=(const std__atomic<T>& other) {
      this->store(other.load());
      return *this;
    }
  };

  template<typename T>
    __forceinline void atomic_min(std::atomic<T>& aref, const T& bref)
  {
    const T b = bref.load();
    while (true) {
      T a = aref.load();
      if (a <= b) break;
      if (aref.compare_exchange_strong(a,b)) break;
    }
  }

  template<typename T>
    __forceinline void atomic_max(std::atomic<T>& aref, const T& bref)
  {
    const T b = bref.load();
    while (true) {
      T a = aref.load();
      if (a >= b) break;
      if (aref.compare_exchange_strong(a,b)) break;
    }
  }
}
