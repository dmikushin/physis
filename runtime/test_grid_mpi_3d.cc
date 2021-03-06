#define PHYSIS_MPI
#include "physis/physis.h"
#include "physis/physis_util.h"
#include "runtime/mpi_runtime.h"
#include "runtime/grid_mpi_debug_util.h"
#include "runtime/mpi_util.h"

#define N (4)
#define NDIM (3)

using namespace std;
using namespace physis::runtime;
using namespace physis;

int my_rank;

static void init_grid(GridMPI *g) {
  int idx = 0;
  float v = N * N * N* my_rank;
  if (g->num_dims() == 3) {
    for (int i = 0; i < g->local_size()[0]; ++i) {
      for (int j = 0; j < g->local_size()[1]; ++j) {
        for (int k = 0; k < g->local_size()[2]; ++k) {
          ((float*)(g->_data()))[idx] = v;
          ++v;
          ++idx;
        }
      }
    }
  } else if  (g->num_dims() == 2) {
    for (int i = 0; i < g->local_size()[0]; ++i) {
      for (int j = 0; j < g->local_size()[1]; ++j) {
        ((float*)(g->_data()))[idx] = v;
        ++v;
        ++idx;
      }
    }
  } else if  (g->num_dims() == 1) {
    for (int i = 0; i < g->local_size()[0]; ++i) {
      ((float*)(g->_data()))[idx] = v;
      ++v;
      ++idx;
    }
  } else {
    LOG_ERROR() << "Unsupported dimension\n";
    exit(1);
  }
}  


void test1() {
  LOG_DEBUG_MPI() << "Test 1: Grid space creation and deletion\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  delete gs;
  LOG_DEBUG_MPI() << "Test 1: Finished\n";
}

void test2() {
  LOG_DEBUG_MPI() << "[" << my_rank << "] Test 2: Grid creation and deletion\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);
  print_grid<float>(g, my_rank, cerr);
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "[" << my_rank << "] Test 2: Finished\n";
}

void test3() {
  LOG_DEBUG_MPI() << "[" << my_rank << "] Test 3: ExchangeBoundaries\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);
  UnsignedArray halo(1, 1, 1);
  gs->ExchangeBoundaries(g->id(), halo, halo, false, false);
  print_grid<float>(g, my_rank, cerr);
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "[" << my_rank << "] Test 3: Finished\n";
}

void test4() {
  LOG_DEBUG_MPI() << "[" << my_rank << "] Test 4: Load subgrid self\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);
  
  // This should not perform any copy.
  GridMPI *g2 = gs->LoadSubgrid(g, g->local_offset(), g->local_size());
  PSAssert(g2 == NULL);
  print_grid<float>(g, my_rank, cerr);
  
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "Test 4: Finished\n";
}

void test5() {
  LOG_DEBUG_MPI() << "Test 5: Overlapping LoadSubgrid\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);
  
  IndexArray goffset = g->local_offset() - 1;
  goffset.SetNoLessThan(0L);
  IndexArray gy = g->local_offset() + g->local_size() + 1;
  gy.SetNoMoreThan(g->size());
  GridMPI *g2 = gs->LoadSubgrid(g, goffset, gy - goffset);
  PSAssert(g2);
  print_grid<float>(g2, my_rank, cerr);
  
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "Test 5: Finished\n";
}

void test6() {
  LOG_DEBUG_MPI() << "Test 6: LoadNeighbor no diagonal points\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);

  GridMPI *g2 = gs->LoadNeighbor(g, IndexArray(1, 1, 1), IndexArray(1, 1, 1), false, false, false);
  if (g2) {
    LOG_ERROR_MPI() << "Neighbor exchange not used\n";
    PSAbort(1);
  }
  print_grid<float>(g, my_rank, cerr);
  
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "Finished\n";
}

void test7() {
  LOG_DEBUG_MPI() << "LoadNeighbor with diagonal points\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);

  GridMPI *g2 = gs->LoadNeighbor(g, IndexArray(1, 1, 1), IndexArray(1, 1, 1), true, false, false);
  if (g2) {
    LOG_ERROR_MPI() << "Neighbor exchange not used\n";
    PSAbort(1);
  }
  print_grid<float>(g, my_rank, cerr);
  
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "Finished\n";
}

void test8() {
  LOG_DEBUG_MPI() << "Fetch same subgrid\n";
  IndexArray global_size(N, N, N);
  IntArray proc_size(2, 2, 2);
  GridSpaceMPI *gs = new GridSpaceMPI(NDIM, global_size, NDIM, proc_size, my_rank);
  IndexArray global_offset;
  GridMPI *g = gs->CreateGrid(PS_FLOAT, sizeof(float), NDIM, global_size,
                              false, global_offset, 0);
  init_grid(g);
  IndexArray goffset;
  LOG_DEBUG_MPI() << "offset: " << goffset << "\n";
  IndexArray gsize(2, 2, 2);
  GridMPI *g2 = gs->LoadSubgrid(g, goffset, gsize);
  if (my_rank == 0) {
    PSAssert(g2 == NULL);
  } else {
    PSAssert(g2);
    print_grid<float>(g2, my_rank, cerr);
  }
  
  delete g;
  delete gs;
  LOG_DEBUG_MPI() << "Finished\n";
}

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  int num_procs;
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
  //assert(num_procs == 8);
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "test1") == 0) {
      test1();
    } else if (strcmp(argv[i], "test2") == 0) {
      test2();
    } else if (strcmp(argv[i], "test3") == 0) {
      test3();
    } else if (strcmp(argv[i], "test4") == 0) {
      test4();
    } else if (strcmp(argv[i], "test5") == 0) {
      test5();
    } else if (strcmp(argv[i], "test6") == 0) {
      test6();
    } else if (strcmp(argv[i], "test7") == 0) {
      test7();
    } else if (strcmp(argv[i], "test8") == 0) {
      test8();
    }
  }
  LOG_DEBUG_MPI() << "Finished\n";  
  MPI_Finalize();
  return 0;
}

