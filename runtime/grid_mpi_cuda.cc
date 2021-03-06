
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#include "runtime/grid_mpi_cuda.h"
#include "runtime/runtime_common_cuda.h"
#include "runtime/buffer_cuda.h"
#include "runtime/grid_util.h"

#include <cuda.h>
#include <cuda_runtime.h>

using namespace physis::runtime::performance;
using std::make_pair;

namespace physis {
namespace runtime {

//
// Grid
//
GridMPICUDA3D::GridMPICUDA3D(
    PSType type, int elm_size, int num_dims, const IndexArray &size,
    bool double_buffering, const IndexArray &global_offset,
    const IndexArray &local_offset, const IndexArray &local_size,
    int attr):
    GridMPI(type, elm_size, num_dims, size, double_buffering, global_offset,
            local_offset, local_size, attr) {
  
  if (empty_) return;

  // These pointers are replaced with buffer substrate in this class
  delete[]  halo_self_fw_;
  halo_self_fw_ = NULL;
  delete[] halo_self_bw_;
  halo_self_bw_ = NULL;

  FixupBufferPointers();
}

GridMPICUDA3D *GridMPICUDA3D::Create(
    PSType type, int elm_size, int num_dims, const IndexArray &size,
    bool double_buffering, const IndexArray &global_offset,
    const IndexArray &local_offset, const IndexArray &local_size,
    int attr) {
  GridMPICUDA3D *gmc = new GridMPICUDA3D(type, elm_size, num_dims, size,
                                         double_buffering, global_offset,
                                         local_offset, local_size,
                                         attr);
  gmc->InitBuffers();
  return gmc;
}


GridMPICUDA3D::~GridMPICUDA3D() {
  DeleteBuffers();
}

void GridMPICUDA3D::DeleteBuffers() {
  if (empty_) return;
  for (int i = 0; i < num_dims_; ++i) {
    for (int j = 0; j < 2; ++j) {
      if (halo_self_cuda_) {
        delete halo_self_cuda_[i][j];
        halo_self_cuda_[i][j] = NULL;
      }
      if (halo_self_mpi_) {
        delete halo_self_mpi_[i][j];
        halo_self_mpi_[i][j] = NULL;
      }
      if (halo_peer_cuda_) {
        delete halo_peer_cuda_[i][j];
        halo_peer_cuda_[i][j] = NULL;
      }
      if (halo_peer_dev_) {
        delete halo_peer_dev_[i][j];
        halo_peer_dev_[i][j] = NULL;
      }
    }
    halo_peer_fw_[i] = NULL;
    halo_peer_bw_[i] = NULL;    
  }
  delete[] halo_self_cuda_;
  halo_self_cuda_ = NULL;
  delete[] halo_self_mpi_;
  halo_self_mpi_ = NULL;
  delete[] halo_peer_cuda_;
  halo_peer_cuda_ = NULL;
  delete[] halo_peer_dev_;
  halo_peer_dev_ = NULL;
  GridMPI::DeleteBuffers();
}


void GridMPICUDA3D::InitBuffers() {
  LOG_DEBUG() << "Initializing grid buffer\n";

  if (empty_) return;
  data_buffer_[0] = new BufferCUDADev3D(num_dims(), elm_size());
  data_buffer_[0]->Allocate(local_size());
  if (double_buffering_) {
    data_buffer_[1] = new BufferCUDADev3D(num_dims(), elm_size());
    data_buffer_[1]->Allocate(local_size());    
  } else {
    data_buffer_[1] = data_buffer_[0];
  }
  
#if USE_MAPPED  
  halo_self_cuda_ = new BufferCUDAHostMapped*[num_dims_][2];
#else
  halo_self_cuda_ = new BufferCUDAHost*[num_dims_][2];  
#endif
  halo_self_mpi_ = new BufferHost*[num_dims_][2];
  halo_peer_cuda_ = new BufferCUDAHost*[num_dims_][2];
  halo_peer_dev_ = new BufferCUDADev*[num_dims_][2];
  for (int i = 0; i < num_dims_; ++i) {
    for (int j = 0; j < 2; ++j) {
#if USE_MAPPED      
      halo_self_cuda_[i][j] = new BufferCUDAHostMapped(elm_size());
#else
      halo_self_cuda_[i][j] = new BufferCUDAHost(elm_size());      
#endif      
      halo_self_mpi_[i][j] = new BufferHost(elm_size());
      halo_peer_cuda_[i][j] = new BufferCUDAHost(elm_size());
      halo_peer_dev_[i][j] = new BufferCUDADev(elm_size());
    }
  }

  FixupBufferPointers();
}

void GridMPICUDA3D::SetCUDAStream(cudaStream_t strm) {
  static_cast<BufferCUDADev3D*>(data_buffer_[0])->strm() = strm;
  for (int i = 0; i < num_dims_; ++i) {
    for (int j = 0; j < 2; ++j) {
      halo_peer_dev_[i][j]->strm() = strm;
    }
  }
}

// Assumes the dst address is in the device memory
void GridMPICUDA3D::Copyin(void *dst, const void *src, size_t size) {
  CUDA_SAFE_CALL(cudaMemcpy(dst, src, size,
                            cudaMemcpyHostToDevice));
}

// Assumes the source address is in the device memory
void GridMPICUDA3D::Copyout(void *dst, const void *src, size_t size) {
  CUDA_SAFE_CALL(cudaMemcpy(dst, src, size,
                            cudaMemcpyDeviceToHost));
}

std::ostream &GridMPICUDA3D::Print(std::ostream &os) const {
  os << "GridMPICUDA {"
     << "elm_size: " << elm_size_
     << ", size: " << size_
     << ", global offset: " << global_offset_
     << ", local offset: " << local_offset_
     << ", local size: " << local_size_
     << "}";
  return os;
}


void GridMPICUDA3D::CopyoutHalo(int dim, unsigned width, bool fw,
                                bool diagonal) {
  PSAssert(num_dims() <= 3);
  halo_has_diagonal_ = diagonal;

  int fw_idx = (fw) ? 1: 0;

  IndexArray halo_offset;
  if (!fw) halo_offset[dim] = local_size_[dim] - width;
  IndexArray halo_size = local_size_;
  halo_size[dim] = width;
  size_t linear_size = halo_size.accumulate(num_dims());

  // First, copy out of CUDA device memory to CUDA pinned host memory
#if USE_MAPPED
  BufferCUDAHostMapped *halo_cuda_host = halo_self_cuda_[dim][fw_idx];    
#else
  BufferCUDAHost *halo_cuda_host = halo_self_cuda_[dim][fw_idx];  
#endif
  BufferHost *halo_mpi_host = halo_self_mpi_[dim][fw_idx];
  halo_cuda_host->EnsureCapacity(linear_size);
  static_cast<BufferCUDADev3D*>(buffer())->Copyout(
      *halo_cuda_host, halo_offset, halo_size);

  // Next, copy out to the halo buffer from CUDA pinned host memory
  halo_mpi_host->EnsureCapacity(CalcHaloSize(dim, width, diagonal));
  if (dim == num_dims_ - 1 || !diagonal) {
    halo_cuda_host->Copyout(*halo_mpi_host,
                            IndexArray(halo_size.accumulate(num_dims_)));
    return;
  }

  switch (num_dims_) {
    case 2:
      //CopyoutHalo2D0(width, fw, *halo_buf);
      PSAbort(1); // TODO: not supported
      break;
    case 3:
      if (dim == 0) {
        CopyoutHalo3D0(width, fw);
      } else if (dim == 1) {
        CopyoutHalo3D1(width, fw);
      } else {
        LOG_ERROR() <<
            "This case should have been handled in the first block"
            " of this function.\n";
        PSAbort(1);
      }
      break;
    default:
      LOG_ERROR() << "Unsupported dimension: " << num_dims_ << "\n";
      PSAbort(1);
  }
}

// REFACTORING: This is almost identical to the parent class implementation. 
void GridMPICUDA3D::CopyoutHalo3D1(unsigned width, bool fw) {
  int nd = 3;
  int fw_idx = fw ? 1 : 0;
  // copy diag
  char *buf = (char*)halo_self_mpi_[1][fw_idx]->Get();
  IndexArray sg_offset;
  if (!fw) sg_offset[1] = local_size_[1] - width;
  IndexArray sg_size(local_size_[0], width, halo_bw_width_[2]);
  IndexArray halo_size(local_size_[0], local_size_[1],
                     halo_bw_width_[2]);
  // different  
  CopyoutSubgrid(elm_size_, num_dims_, halo_peer_cuda_[2][0]->Get(),
                 halo_size, buf, sg_offset, sg_size);
  buf += sg_size.accumulate(nd) * elm_size_;

  // copy halo
  sg_size[2] = local_size_[2];
  // different
  halo_self_cuda_[1][fw_idx]->Copyout(buf, IndexArray(sg_size.accumulate(nd)));
  buf += sg_size.accumulate(nd) * elm_size_;
  
  // copy diag
  sg_size[2] = halo_fw_width_[2];
  halo_size[2] = halo_fw_width_[2];
  CopyoutSubgrid(elm_size_, num_dims_, halo_peer_cuda_[2][1]->Get(),
                 halo_size, buf, sg_offset, sg_size);
}

// REFACTORING: This is almost identical to the parent class implementation. 
//void GridMPICUDA3D::CopyoutHalo3D0(unsigned width, int fw) {
void GridMPICUDA3D::CopyoutHalo3D0(unsigned width, bool fw) {
  int fw_idx = fw ? 1 : 0;
  char *buf = (char*)halo_self_mpi_[0][fw_idx]->Get();
  size_t line_size = elm_size_ * width;  
  size_t xoffset = fw ? 0 : local_size_[0] - width;
  char *halo_bw_1 = (char*)halo_peer_cuda_[1][0]->Get() + xoffset * elm_size_;
  char *halo_fw_1 = (char*)halo_peer_cuda_[1][1]->Get() + xoffset * elm_size_;
  char *halo_bw_2 = (char*)halo_peer_cuda_[2][0]->Get() + xoffset * elm_size_;
  char *halo_fw_2 = (char*)halo_peer_cuda_[2][1]->Get() + xoffset * elm_size_;
  //char *halo_0 = data_[0] + xoffset * elm_size_;
  char *halo_0 = (char*)halo_self_cuda_[0][fw_idx]->Get();
  for (PSIndex k = 0; k < (PSIndex)halo_bw_width_[2]+
           local_size_[2]+(PSIndex)halo_fw_width_[2]; ++k) {
    // copy from the 2nd-dim backward halo 
    char *t = halo_bw_1;
    for (unsigned j = 0; j < halo_bw_width_[1]; j++) {
      memcpy(buf, t, line_size);
      buf += line_size;      
      t += local_size_[0] * elm_size_;
    }
    halo_bw_1 += local_size_[0] * halo_bw_width_[1] * elm_size_;

    char **h;
    size_t halo_x_size;
    // select halo source
    if (k < (PSIndex)halo_bw_width_[2]) {
      h = &halo_bw_2;
      halo_x_size = local_size_[0] * elm_size_;
    } else if (k < (PSIndex)halo_bw_width_[2] + local_size_[2]) {
      h = &halo_0;
      halo_x_size = elm_size_ * width;
    } else {
      h = &halo_fw_2;
      halo_x_size = local_size_[0] * elm_size_;
    }
    t = *h;
    for (PSIndex j = 0; j < local_size_[1]; ++j) {
      memcpy(buf, t, line_size);
      buf += line_size;
      t += halo_x_size;
    }
    *h += halo_x_size * local_size_[1];

    // copy from the 2nd-dim forward halo 
    t = halo_fw_1;
    for (unsigned j = 0; j < halo_fw_width_[1]; j++) {
      memcpy(buf, t, line_size);
      buf += line_size;      
      t += local_size_[0] * elm_size_;
    }
    halo_fw_1 += local_size_[0] * halo_fw_width_[1] * elm_size_;
  }
}

void GridMPICUDA3D::FixupBufferPointers() {
  LOG_VERBOSE() << "Fixup buffer ptrs\n";
  if (data_buffer_[0]) {
    if (data_[0] != (char*)data_buffer_[0]->Get()) {
      LOG_DEBUG() << "Buf pointer updated by " <<
	data_buffer_[0]->Get() << "\n";
    }
    data_[0] = (char*)data_buffer_[0]->Get();
    data_[1] = (char*)data_buffer_[1]->Get();
  } else {
    LOG_VERBOSE() << "data buffer null\n";
    data_[0] = NULL;
    data_[1] = NULL;
  }
  
  dev_.p0 = data_[0];
#ifdef AUTO_DOUBLE_BUFFERING
  dev_.p1 = data_[1];
#endif
  dev_.diag = halo_has_diagonal();
  LOG_VERBOSE() << "Diag: " << dev_.diag << "\n";
  if (data_buffer_[0]) {
    dev_.pitch = static_cast<BufferCUDADev3D*>(buffer())
        ->GetPitch() / elm_size();
    LOG_DEBUG() << "Pitch: " << dev_.pitch << "\n";
    for (int i = 0; i < num_dims(); ++i) {
      dev_.dim[i]  = size()[i];
      dev_.local_size[i] = local_size()[i];
      dev_.local_offset[i] = local_offset()[i];      
      halo_peer_fw_[i] = (char*)halo_peer_dev_[i][1]->Get();
      halo_peer_bw_[i] = (char*)halo_peer_dev_[i][0]->Get();

      for (int j = 0; j < 2; ++j) {
        dev_.halo[i][j] = halo_peer_dev_[i][j]->Get();
      }
      dev_.halo_width[i][1] = halo_fw_width()[i];
      dev_.halo_width[i][0] = halo_bw_width()[i];
    }
  }
}

void GridMPICUDA3D::EnsureRemoteGrid(const IndexArray &local_offset,
                                     const IndexArray &local_size) {
  if (remote_grid_ == NULL) {
    remote_grid_ = GridMPICUDA3D::Create(type_, elm_size_, num_dims_,
                                         size_, false, global_offset_,
                                         local_offset, local_size,
                                         0);
  } else {
    remote_grid_->Resize(local_offset, local_size);
  }
}

void GridMPICUDA3D::Save() {
  LOG_DEBUG() << "Saving grid\n";
  Buffer *buf = static_cast<BufferCUDADev3D*>(
      data_buffer_[0]);
  // Copyout
  void *data = buf->Copyout();
  SaveToFile(data, buf->GetLinearSize());
  free(data);
  DeleteBuffers();
}

void GridMPICUDA3D::Restore() {
  LOG_DEBUG() << "Restoring grid\n";
  LOG_DEBUG() << "Initializing buffer\n";
  InitBuffers();
  LOG_DEBUG() << "Buffer initialized\n";
  // Read from a file
  BufferCUDADev3D *buf = static_cast<BufferCUDADev3D*>(
      data_buffer_[0]);
  void *data = malloc(buf->GetLinearSize());
  RestoreFromFile(data, buf->GetLinearSize());
  // Copyin
  buf->Copyin(data, IndexArray((PSIndex)0), buf->size());
}

//
// Grid Space
//

GridSpaceMPICUDA::GridSpaceMPICUDA(
    int num_dims, const IndexArray &global_size,
    int proc_num_dims, const IntArray &proc_size, int my_rank):
    GridSpaceMPI(num_dims, global_size, proc_num_dims,
                 proc_size, my_rank) {
  buf = new BufferHost(1);
}

GridSpaceMPICUDA::~GridSpaceMPICUDA() {
  delete buf;
  FOREACH (it, load_neighbor_prof_.begin(), load_neighbor_prof_.end()) {
    delete[] it->second;
  }
}

GridMPICUDA3D *GridSpaceMPICUDA::CreateGrid(
    PSType type, int elm_size, int num_dims, const IndexArray &size,
    bool double_buffering, const IndexArray &global_offset,
    int attr) {
  IndexArray grid_size = size;
  IndexArray grid_global_offset = global_offset;
  if (num_dims_ < num_dims) {
    // The grid space has smaller dimension.
    LOG_ERROR() << "Cannot create " << num_dims << "-d grid in "
                << num_dims_ << "-d space.\n";
    PSAbort(1);
  } else if (num_dims_ > num_dims) {
    // Adapt the dimension of grid to the grid space
    for (int i = num_dims; i < num_dims_; ++i) {
      grid_size[i] = 1;
      grid_global_offset[i] = 0;
    }
    num_dims = num_dims_;
  }

  IndexArray local_offset, local_size;  
  PartitionGrid(num_dims, grid_size, grid_global_offset,
                local_offset, local_size);

  LOG_DEBUG() << "local_size: " << local_size << "\n";
  GridMPICUDA3D *g = GridMPICUDA3D::Create(type, elm_size, num_dims, grid_size,
                                           double_buffering,
                                           grid_global_offset, local_offset,
                                           local_size, attr);
  LOG_DEBUG() << "grid created\n";
  RegisterGrid(g);
  LOG_DEBUG() << "grid registered\n";  
  DataCopyProfile *profs = new DataCopyProfile[num_dims*2];
  load_neighbor_prof_.insert(make_pair(g->id(), profs));
  return g;
}

// Just copy out halo from GPU memory
void GridSpaceMPICUDA::ExchangeBoundariesStage1(
    GridMPI *g, int dim, unsigned halo_fw_width,
    unsigned halo_bw_width, bool diagonal,
    bool periodic) const {

  GridMPICUDA3D *grid = static_cast<GridMPICUDA3D*>(g);
  if (grid->empty_) return;

  LOG_DEBUG() << "Periodic grid?: " << periodic << "\n";

  DataCopyProfile *profs = find<int, DataCopyProfile*>(
      load_neighbor_prof_, g->id(), NULL);
  DataCopyProfile &prof_upw = profs[dim*2];
  DataCopyProfile &prof_dwn = profs[dim*2+1];  
  Stopwatch st;
  // Sends out the halo for backward access

  if (halo_bw_width > 0 &&
      (periodic ||
       grid->local_offset_[dim] + grid->local_size_[dim] < grid->size_[dim])) {
#if defined(PS_VERBOSE)            
    LOG_DEBUG() << "Sending halo of " << bw_size << " elements"
              << " for bw access to " << fw_peer << "\n";
#endif    
    st.Start();
    grid->CopyoutHalo(dim, halo_bw_width, false, diagonal);
    prof_upw.gpu_to_cpu += st.Stop();
  }
  
  // Sends out the halo for forward access
  if (halo_fw_width > 0 &&
      (periodic || grid->local_offset_[dim] > 0)) {
#if defined(PS_VERBOSE)            
    LOG_DEBUG() << "Sending halo of " << fw_size << " elements"
                << " for fw access to " << bw_peer << "\n";
#endif    
    st.Start();    
    grid->CopyoutHalo(dim, halo_fw_width, true, diagonal);
    prof_dwn.gpu_to_cpu += st.Stop();
  }
}

// Exchanges halos on CPU memory, and copy in the new halo to GPU
// memory. Assumes the new halo to exchange is already copied out of
// GPU memory at Stage 1.
// Note: width is unsigned. 
void GridSpaceMPICUDA::ExchangeBoundariesStage2(
    GridMPI *g, int dim, unsigned halo_fw_width,
    unsigned halo_bw_width, bool diagonal, bool periodic) const {

  GridMPICUDA3D *grid = static_cast<GridMPICUDA3D*>(g);
  if (grid->empty_) return;

  int fw_peer = fw_neighbors_[dim];
  int bw_peer = bw_neighbors_[dim];
  ssize_t fw_size = grid->CalcHaloSize(dim, halo_fw_width, diagonal);
  ssize_t bw_size = grid->CalcHaloSize(dim, halo_bw_width, diagonal);
  LOG_DEBUG() << "Periodic grid?: " << periodic << "\n";

  DataCopyProfile *profs = find<int, DataCopyProfile*>(
      load_neighbor_prof_, g->id(), NULL);
  DataCopyProfile &prof_upw = profs[dim*2];
  DataCopyProfile &prof_dwn = profs[dim*2+1];  
  Stopwatch st;

  bool req_bw_active = false;
  bool req_fw_active = false;
  MPI_Request req_bw;
  MPI_Request req_fw;
  
  if (halo_bw_width > 0 &&
      (periodic ||
       grid->local_offset_[dim] + grid->local_size_[dim] < grid->size_[dim])) {
    st.Start();
    grid->halo_self_mpi_[dim][0]->MPIIsend(fw_peer, comm_, &req_bw,
                                           IndexArray(), IndexArray(bw_size));
    prof_upw.cpu_out += st.Stop();
    req_bw_active = true;
  }
  
  // Sends out the halo for forward access
  if (halo_fw_width > 0 &&
      (periodic || grid->local_offset_[dim] > 0)) {
    st.Start();        
    grid->halo_self_mpi_[dim][1]->MPIIsend(bw_peer, comm_, &req_fw,
                                           IndexArray(), IndexArray(fw_size));
    prof_dwn.cpu_out += st.Stop();
    req_fw_active = true;
  }

  // Receiving halo for backward access
  RecvBoundaries(grid, dim, halo_bw_width, false, diagonal,
                 periodic, bw_size, prof_dwn);
  
  // Receiving halo for forward access
  RecvBoundaries(grid, dim, halo_fw_width, true, diagonal,
                 periodic, fw_size, prof_upw);

  // Ensure the exchanges are done. Otherwise, the send buffers can be
  // overwritten by the next iteration.
  if (req_fw_active) CHECK_MPI(MPI_Wait(&req_fw, MPI_STATUS_IGNORE));
  if (req_bw_active) CHECK_MPI(MPI_Wait(&req_bw, MPI_STATUS_IGNORE));

  grid->FixupBufferPointers();
  return;
}

bool GridSpaceMPICUDA::SendBoundaries(GridMPICUDA3D *grid, int dim,
                                      unsigned width,
                                      bool forward, bool diagonal,
                                      bool periodic,
                                      ssize_t halo_size,
                                      DataCopyProfile &prof,
                                      MPI_Request &req) const {
  // Nothing to do since the width is zero
  if (width <= 0) {
    return false;
  }

  // Do nothing if this process is on the end of the dimension and
  // periodic boundary is not set.
  if (!periodic) {
    if ((!forward &&
         grid->local_offset_[dim] + grid->local_size_[dim] == grid->size_[dim]) ||
        (forward && grid->local_offset_[dim] == 0)) {
      return false;
    }
  }
  
  int dir_idx = forward ? 1 : 0;
  int peer = forward ? bw_neighbors_[dim] : fw_neighbors_[dim];  
  LOG_DEBUG() << "Sending halo of " << halo_size << " elements"
              << " for access to " << peer << "\n";
  Stopwatch st;  
  st.Start();
  grid->CopyoutHalo(dim, width, forward, diagonal);
  prof.gpu_to_cpu += st.Stop();
#if defined(PS_VERBOSE)
  LOG_VERBOSE() << "cuda ->";
  grid->halo_self_cuda_[dim][dir_idx]->print<float>(std::cerr);
  LOG_VERBOSE() << "host ->";
  grid->halo_self_mpi_[dim][dir_idx]->print<float>(std::cerr);
#endif

  st.Start();
  grid->halo_self_mpi_[dim][dir_idx]->MPIIsend(peer, comm_, &req,
                                               IndexArray(),
                                               IndexArray(halo_size));
  prof.cpu_out += st.Stop();
  return true;
}

bool GridSpaceMPICUDA::RecvBoundaries(GridMPICUDA3D *grid, int dim,
                                      unsigned width,
                                      bool forward, bool diagonal,
                                      bool periodic, ssize_t halo_size,
                                      DataCopyProfile &prof) const {
  int peer = forward ? fw_neighbors_[dim] : bw_neighbors_[dim];
  int dir_idx = forward ? 1 : 0;
  bool is_last_process =
      grid->local_offset_[dim] + grid->local_size_[dim]
      == grid->size_[dim];
  bool is_first_process = grid->local_offset_[dim] == 0;
  Stopwatch st;

  if (width == 0 ||
      (!periodic && ((forward && is_last_process) ||
                        (!forward && is_first_process)))) {
    if (forward) {
      grid->halo_fw_width_[dim] = 0;
      grid->halo_fw_size_[dim].Set(0);
    } else {
      grid->halo_bw_width_[dim] = 0;
      grid->halo_bw_size_[dim].Set(0);
    }
    return false;
  }

#if defined(PS_DEBUG)
  if (!periodic) {
    if( (forward && grid->local_offset_[dim] +
         grid->local_size_[dim] + (PSIndex)width > grid->size_[dim]) ||
        (!forward && grid->local_offset_[dim] - width < 0)
        ) {
      LOG_ERROR() << "Off limit accesses: "
                  << "local_offset: " << grid->local_offset_[dim]
                  << ", local_size: " << grid->local_size_[dim]
                  << ", width: " << width
                  << ", grid size: " << grid->size_[dim]
                  << "\n";
      LOG_ERROR() << "is_first: " << is_first_process
                  << ", is_last: " << is_last_process
                  << ", forward: " << forward
                  << "\n";
      PSAbort(1);
    }
  }
#endif  
  LOG_DEBUG() << "Receiving halo of " << halo_size
              << " bytes from " << peer << "\n";
  
  grid->halo_peer_cuda_[dim][dir_idx]->EnsureCapacity(halo_size);
  if (forward) {
    grid->halo_fw_width_[dim] = width;
  } else {
    grid->halo_bw_width_[dim] = width;
  }
  grid->SetHaloSize(dim, forward, width, diagonal);
  st.Start();
  grid->halo_peer_cuda_[dim][dir_idx]->
      Buffer::MPIRecv(peer, comm_, IndexArray(halo_size));
  prof.cpu_in += st.Stop();
  st.Start(); 
  grid->halo_peer_dev_[dim][dir_idx]->
      Copyin(*grid->halo_peer_cuda_[dim][dir_idx], IndexArray(),
             IndexArray(halo_size));
  prof.cpu_to_gpu += st.Stop();
  return true;
}

// Note: width is unsigned. 
void GridSpaceMPICUDA::ExchangeBoundaries(
    GridMPI *g, int dim, unsigned halo_fw_width,
    unsigned halo_bw_width, bool diagonal, bool periodic) const {

  GridMPICUDA3D *grid = static_cast<GridMPICUDA3D*>(g);
  if (grid->empty_) return;

  ssize_t fw_size = grid->CalcHaloSize(dim, halo_fw_width, diagonal);
  ssize_t bw_size = grid->CalcHaloSize(dim, halo_bw_width, diagonal);
  LOG_DEBUG() << "Periodic grid?: " << periodic << "\n";

  DataCopyProfile *profs = find<int, DataCopyProfile*>(
      load_neighbor_prof_, g->id(), NULL);
  DataCopyProfile &prof_upw = profs[dim*2];
  DataCopyProfile &prof_dwn = profs[dim*2+1];  
  MPI_Request req_bw, req_fw;

  // Sends out the halo for backward access
  bool req_bw_active =
      SendBoundaries(grid, dim, halo_bw_width, false, diagonal,
                     periodic, bw_size, prof_upw, req_bw);
  
  // Sends out the halo for forward access
  bool req_fw_active =
      SendBoundaries(grid, dim, halo_fw_width, true, diagonal,
                     periodic, fw_size, prof_dwn, req_fw);

  // Receiving halo for backward access
  RecvBoundaries(grid, dim, halo_bw_width, false, diagonal,
                 periodic, bw_size, prof_dwn);
  
  // Receiving halo for forward access
  RecvBoundaries(grid, dim, halo_fw_width, true, diagonal,
                 periodic, fw_size, prof_upw);

  // Ensure the exchanges are done. Otherwise, the send buffers can be
  // overwritten by the next iteration.
  if (req_fw_active) CHECK_MPI(MPI_Wait(&req_fw, MPI_STATUS_IGNORE));
  if (req_bw_active) CHECK_MPI(MPI_Wait(&req_bw, MPI_STATUS_IGNORE));

  grid->FixupBufferPointers();
  return;
}

void GridSpaceMPICUDA::HandleFetchRequest(GridRequest &req, GridMPI *g) {
  LOG_DEBUG() << "HandleFetchRequest\n";
  GridMPICUDA3D *gm = static_cast<GridMPICUDA3D*>(g);
  FetchInfo finfo;
  int nd = num_dims_;
  CHECK_MPI(MPI_Recv(&finfo, sizeof(FetchInfo), MPI_BYTE,
                     req.my_rank, 0, comm_, MPI_STATUS_IGNORE));
  size_t bytes = finfo.peer_size.accumulate(nd) * g->elm_size();
  buf->EnsureCapacity(bytes);
  static_cast<BufferCUDADev3D*>(gm->buffer())->Copyout(
      *buf, finfo.peer_offset - g->local_offset(), finfo.peer_size);
  SendGridRequest(my_rank_, req.my_rank, comm_, FETCH_REPLY);
  MPI_Request mr;
  buf->MPIIsend(req.my_rank, comm_, &mr, IndexArray(), IndexArray(bytes));
  //CHECK_MPI(PS_MPI_Isend(buf, bytes, MPI_BYTE, req.my_rank, 0, comm_, &mr));
}

void GridSpaceMPICUDA::HandleFetchReply(GridRequest &req, GridMPI *g,
                                        std::map<int, FetchInfo> &fetch_map,
                                        GridMPI *sg) {
  LOG_DEBUG() << "HandleFetchReply\n";
  GridMPICUDA3D *sgm = static_cast<GridMPICUDA3D*>(sg);
  const FetchInfo &finfo = fetch_map[req.my_rank];
  PSAssert(GetProcessRank(finfo.peer_index) == req.my_rank);
  size_t bytes = finfo.peer_size.accumulate(num_dims_) * g->elm_size();
  LOG_DEBUG() << "Fetch reply data size: " << bytes << "\n";
  buf->EnsureCapacity(bytes);
  buf->Buffer::MPIRecv(req.my_rank, comm_, IndexArray(bytes));
  LOG_DEBUG() << "Fetch reply received\n";
  static_cast<BufferCUDADev3D*>(sgm->buffer())->Copyin(
      *buf, finfo.peer_offset - sg->local_offset(),
      finfo.peer_size);
  sgm->FixupBufferPointers();
  return;
}

inline PSIndex GridCalcOffset3D(const IndexArray &index,
                                const IndexArray &size,
                                size_t pitch) {
  return index[0] + index[1] * pitch + index[2] * pitch * size[1];
}

void *GridMPICUDA3D::GetAddress(const IndexArray &indices_param) {
  // Use the remote grid if remote_grid_active is true.
  if (remote_grid_active()) {
    PSAbort(1);
  }
  
  IndexArray indices = indices_param -local_offset();
  bool diag = halo_has_diagonal();
  for (int i = 0; i < num_dims(); ++i) {
    if (indices[i] < 0 || indices[i] >= local_size()[i]) {
      for (int j = i+1; j < PS_MAX_DIM; ++j) {
        if (diag) indices[i] += halo_bw_width()[i];
      }
      PSIndex offset;
      char *buf;
      if (indices[i] < 0) {
        indices[i] += halo_bw_width()[i];
        offset = GridCalcOffset3D(indices, halo_bw_size()[i]);
        buf = _halo_peer_bw()[i];
      } else {
        indices[i] -= local_size()[i];
        offset = GridCalcOffset3D(indices, halo_fw_size()[i]);
        buf = _halo_peer_fw()[i];
      }
      return (void*)(buf + offset*elm_size());
    }
  }

  size_t pitch =  static_cast<BufferCUDADev3D*>(buffer())->GetPitch()
      / elm_size_;
  
  return (void*)(_data() +
                 GridCalcOffset3D( indices, local_size(), pitch)
                 * elm_size_);

}

GridMPI *GridSpaceMPICUDA::LoadNeighbor(
    GridMPI *g,
    const IndexArray &offset_min,
    const IndexArray &offset_max,
    bool diagonal,  bool reuse, bool periodic,
    cudaStream_t cuda_stream) {
  
  // set the stream of buffer by the stream parameter
  GridMPICUDA3D *gmc = static_cast<GridMPICUDA3D*>(g);
  gmc->SetCUDAStream(cuda_stream);
  GridMPI *rg = GridSpaceMPI::LoadNeighbor(g, offset_min, offset_max,
                                           diagonal, reuse, periodic);

  gmc->SetCUDAStream(0);
  return rg;
}

GridMPI *GridSpaceMPICUDA::LoadNeighborStage1(
    GridMPI *g,
    const IndexArray &offset_min,
    const IndexArray &offset_max,
    bool diagonal,  bool reuse, bool periodic,
    cudaStream_t cuda_stream) {
  
  // set the stream of buffer by the stream parameter
  GridMPICUDA3D *gmc = static_cast<GridMPICUDA3D*>(g);
  gmc->SetCUDAStream(cuda_stream);

  IndexArray halo_fw_width(offset_max);
  halo_fw_width.SetNoLessThan(0);  
  IndexArray halo_bw_width(offset_min);
  halo_bw_width = halo_bw_width * -1;
  halo_bw_width.SetNoLessThan(0);
  
  //for (int i = g->num_dims_ - 1; i >= 0; --i) {
  for (int i = g->num_dims_ - 2; i >= 0; --i) {  
    LOG_VERBOSE() << "Exchanging dimension " << i << " data\n";
    ExchangeBoundariesStage1(g, i, halo_fw_width[i], halo_bw_width[i],
                             diagonal, periodic);
  }
  gmc->SetCUDAStream(0);
  return NULL;
}

GridMPI *GridSpaceMPICUDA::LoadNeighborStage2(
    GridMPI *g,
    const IndexArray &offset_min,
    const IndexArray &offset_max,
    bool diagonal,  bool reuse, bool periodic,
    cudaStream_t cuda_stream) {
  
  // set the stream of buffer by the stream parameter
  GridMPICUDA3D *gmc = static_cast<GridMPICUDA3D*>(g);
  gmc->SetCUDAStream(cuda_stream);

  IndexArray halo_fw_width(offset_max);
  halo_fw_width.SetNoLessThan(0);  
  IndexArray halo_bw_width(offset_min);
  halo_bw_width = halo_bw_width * -1;
  halo_bw_width.SetNoLessThan(0);

  // Does not perform staging for the continuous dimension. 
  int i = g->num_dims_ - 1;
  ExchangeBoundaries(g, i, halo_fw_width[i],
                     halo_bw_width[i], diagonal, periodic);

  // For the non-continuous dimensions, finish the exchange steps 
  for (int i = g->num_dims_ - 2; i >= 0; --i) {
    LOG_VERBOSE() << "Exchanging dimension " << i << " data\n";
    ExchangeBoundariesStage2(g, i, halo_fw_width[i],
                             halo_bw_width[i], diagonal, periodic);
  }
  gmc->SetCUDAStream(0);
  return NULL;
}



std::ostream& GridSpaceMPICUDA::PrintLoadNeighborProf(std::ostream &os) const {
  StringJoin sj("\n");
  FOREACH (it, load_neighbor_prof_.begin(), load_neighbor_prof_.end()) {
    int grid_id = it->first;
    DataCopyProfile *profs = it->second;
    StringJoin sj_grid;
    for (int i = 0; i < num_dims_*2; i+=2) {
      sj_grid << "upw: " << profs[i] << ", dwn: " << profs[i+1];
    }
    sj << grid_id << ": " << sj_grid.str();
  }
  return os << sj.str() << "\n";
}

int GridSpaceMPICUDA::ReduceGrid(void *out, PSReduceOp op,
                                 GridMPI *g) {
  void *p = malloc(g->elm_size());
  if (g->Reduce(op, p) == 0) {
    switch (g->type()) {
      case PS_FLOAT:
        *(float*)p = GetReductionDefaultValue<float>(op);
        break;
      case PS_DOUBLE:
        *(double*)p = GetReductionDefaultValue<float>(op);
        break;
      default:
        PSAbort(1);
    }
  }
  MPI_Datatype type = GetMPIDataType(g->type());
  MPI_Op mpi_op = GetMPIOp(op);
  PS_MPI_Reduce(p, out, 1, type, mpi_op, 0, comm_);
  free(p);
  return g->num_elms();
}


} // namespace runtime
} // namespace physis

