#ifndef _FTK_DUF_HH
#define _FTK_DUF_HH

#include <ftk/config.hh>
#include <map>
#include <mutex>

namespace ftk {

template <typename T>
struct duf {
  duf(diy::mpi::communicator c = MPI_COMM_WORLD) : comm(c) {}

  void unite(T, T);
  T find(T) const;
  
  size_t size() const { return parents.size(); }
  bool has(T i) const; //  { return parents.find(i) != parents.end(); }

  void sync();
  // void print() const;

  std::set<T> get_roots() const;

private:
  mutable std::map<T, T> parents; // parents in the local process
  mutable std::mutex mutex;

  diy::mpi::communicator comm;
};

//////
template <typename T>
bool duf<T>::has(T i) const
{
  std::lock_guard<std::mutex> guard(mutex);
  return parents.find(i) != parents.end();
}

template <typename T>
void duf<T>::unite(T i, T j)
{
  i = find(i); // i <-- root(i)
  j = find(j); // j <-- root(j)

  if (i > j) std::swap(i, j); // ensure i<j

  // critical section
  {
    std::lock_guard<std::mutex> guard(mutex);
    parents[j] = i; 
  }
}

template <typename T>
T duf<T>::find(T i) const
{
  std::lock_guard<std::mutex> guard(mutex);
  
  if (parents.find(i) == parents.end()) { // if i does not exist, insert i to the graph and return i as the root
    parents.insert({i, i}); // parents[i] = i;
    return i;
  } else {
    while (i != parents[i]) {
      parents[i] = parents[parents[i]];
      i = parents[i];
    }
    return i;
  }
}

#if 0
template <typename T>
void duf<T>::print() const
{
  for (const auto &kv : parents) 
    fprintf(stderr, "rank=%d, val=%d, root=%d\n", 
        comm.rank(), kv.first, find(kv.second));
}
#endif

template <typename T>
std::set<T> duf<T>::get_roots() const
{
  std::set<T> roots;
  for (const auto &kv : parents) {
    if (kv.first == find(kv.second))
      roots.insert(kv.first);
  }
  return roots;
}

template <typename T>
void duf<T>::sync()
{
  if (comm.size() == 1) return; // no need to sync

  while (1) {
    std::set<T> local_roots = get_roots();
    std::set<T> remote_roots;
    diy::mpi::allgather(comm, local_roots, remote_roots);
    // fprintf(stderr, "rank=%d, #remote_roots=%d\n", comm.rank(), remote_roots.size());

    std::map<T, T> local_updated_parents, remote_updated_parents;
    for (const auto v : remote_roots) {
      if (find(v) != v) {
        local_updated_parents[v] = find(v);
        // fprintf(stderr, "rank=%d, updating %d\n", comm.rank(), v);
      }
    }
    
    diy::mpi::allgather(comm, local_updated_parents, remote_updated_parents);
    // fprintf(stderr, "rank=%d, #updated_remote_parents=%d\n", comm.rank(), remote_updated_parents.size());
    for (const auto &kv : remote_updated_parents) {
      parents[kv.first] = kv.second;
    }

    if (remote_updated_parents.size() == 0)
      break;
  }
}

}

#endif
