#ifndef _FTK_KD_LITE_HH
#define _FTK_KD_LITE_HH

// kd impl that can be used in a cuda kernel

#include <ftk/config.hh>
#include <ftk/numeric/vector_norm.hh>

namespace ftk {

template <int nd, typename I=int, typename F=double>
__host__
void kd_build_recursive(
    const I current,
    const F *X, // coordinates
    const I level, // the current level
    const I offset, // the current offset, 0 for the root pass
    const I length, // the current length
    I *heap, // out: heap
    I *ids) // out: pre-allocated array for ids
{
  const I axis = level % nd;
  const I half = length / 2;
  
  // fprintf(stderr, "current=%d, offset=%d, length=%d\n", 
  //     current, offset, length);

  std::nth_element(
      ids + offset, 
      ids + offset + half, // median
      ids + offset + length, 
      [X, axis](I i, I j) {
        return X[i*nd+axis] < X[j*nd+axis];
      });

  heap[current] = ids[offset + half]; // the median
  // fprintf(stderr, "current=%d, offset=%d, length=%d, median=%d\n", current, offset, length, heap[current]);

  if (half >= 1)
    kd_build_recursive<nd, I, F>(current*2+1, X, level+1, offset,        half-1,        heap, ids); // left
  if (length - half >= 1)  
    kd_build_recursive<nd, I, F>(current*2+2, X, level+1, offset+half+1, length-half-1, heap, ids); // right
}

template <int nd, typename I=int, typename F=double>
__host__
void kd_build(
    const I n, // number of points
    const F *X, // coordinates
    I *heap) // out: pre-allocated heap
{
  std::vector<I> ids;
  ids.resize(n);
  for (int i = 0; i < n; i ++)
    ids[i] = i;

  kd_build_recursive<nd, I, F>(0, X, 0, 0, n, heap, ids.data());
}

template <int nd, typename I=int, typename F=double>
__device__ __host__
I kd_nearest(I n, const F *X, const I *heap, const F *x)
{
  static size_t max_queue_size = 32768; // TODO
  typedef struct {
    I i, depth;
  } qnode_t;

  qnode_t Q[max_queue_size];
  I iq = 0, jq = 0; // start and end of the queue

  Q[jq].i = 0; Q[jq].depth = 0;  // push the root node
  jq = (jq+1) % max_queue_size;
  
  I best = -1; // no best yet
  F best_d2 = 1e32; // no best distance yet

  while (iq != jq) { // Q is not empty
    // fprintf(stderr, "iq=%d, jq=%d\n", iq, jq);

    qnode_t current = Q[iq]; // Q.pop
    iq = (iq+1) % max_queue_size;

    const I i = current.i;
    const I xid = heap[i];
    // fprintf(stderr, "current i %d, xid=%d\n", i, xid);
    const I axis = current.depth % nd;
    I next, other;

    if (x[axis] < X[nd*xid+axis]) {
      next = i * 2 + 1; // left child
      other = i * 2 + 2; // right child
    } else {
      next = i * 2 + 2; // right child
      other = i * 2 + 1; // left child
    }

    const F d2 = vector_dist_2norm2<F>(nd, x, X + nd*xid); // distance to the current node
    if (d2 < best_d2) {
      best = xid;
      best_d2 = d2;

      // fprintf(stderr, "current_best=%d, d2=%f, X=%f, %f, %f\n", 
      //     best, best_d2, 
      //     X[nd*xid], X[nd*xid+1], X[nd*xid+2]);
    }
      
    // const F dp = x[axis] - X[nd*xid+axis]; // distance to the median
    // const F dp2 = dp * dp;

    if (heap[next] >= 0 && next < 2*n) { // the next node exists
      Q[jq].i = next;
      // fprintf(stderr, "adding next %d\n", next);
      Q[jq].depth = current.depth + 1;
      jq = (jq+1) % max_queue_size;
    }

    if (heap[other] >= 0 && other < 2*n) {
      const F dp = x[axis] - X[nd*xid+axis];
      const F dp2 = dp * dp;

      if (dp2 <= best_d2) {
        Q[jq].i = other;
        // fprintf(stderr, "adding other %d\n", other);
        Q[jq].depth = current.depth + 1;
        jq = (jq+1) % max_queue_size;
      }
    }
  }

  return best;
}

} // namespace

#endif
