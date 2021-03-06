// Copyright 2011-2012, RIKEN AICS.
// All rights reserved.
//
// This file is distributed under the BSD license. See LICENSE.txt for
// details.

#ifndef PHYSIS_RUNTIME_BUFFER_H_
#define PHYSIS_RUNTIME_BUFFER_H_

#include "runtime/runtime_common.h"

namespace physis {
namespace runtime {

class Buffer {
 protected:
  typedef void (*BufferDeleter)(void *);
  //! Create a buffer
  explicit Buffer(BufferDeleter deleter);
 public:
  virtual ~Buffer();
  /*! Allocate a new buffer.
    
    \param size Size of the buffer in bytes.
  */
  virtual void Allocate(size_t size);  
  //! Get the pointer to the data memory chunk.
  const void *Get() const { return buf_; }
  //! Get the pointer to the data memory chunk.
  void *Get() { return buf_; }
  //! Returns the size of buffer
  size_t size() const { return size_; }
  //! Free the buffer.
  /*!
    Since this is called from the destructor, it must not be a virtual
    function.
   */
  void Free();
  virtual void EnsureCapacity(size_t size);
  virtual void Shrink(size_t size);
 protected:
  virtual void *GetChunk(size_t size) = 0;

  //! Size of the buffer in bytes.
  size_t size_;
  //! Memory chunk for the buffer data.
  void *buf_;
 private:
  //! Function to delete the buffer
  /*!
    This is used by Delete function, which must be non virtual because
    it is called from the destructor. We still want to use a
    class-specific delete method so it is set in this variable when
    each object is constructed. For example, it is set to the free
    function in libc for BufferHOst object.
  */
  void (*deleter_)(void *ptr);
};

class BufferHost: public Buffer {
 public:
  BufferHost();
  virtual ~BufferHost();
  template <class T>
  std::ostream& print(std::ostream &os) const {
    StringJoin sj;
    T *p = static_cast<T*>(buf_);
    for (int i = 0; i < size_ / sizeof(T); ++i) {
      sj << p[i];
    }
    os << "{" << sj.str() << "}";
    return os;
  }
  
 protected:
  virtual void *GetChunk(size_t size);
};

} // namespace runtime
} // namespace physis


#endif /* PHYSIS_RUNTIME_BUFFER_H_ */
