#ifndef _FTK_XGC_BLOB_FILAMENT_TRACKER_HH
#define _FTK_XGC_BLOB_FILAMENT_TRACKER_HH

#include <ftk/ftk_config.hh>
#include <ftk/algorithms/cca.hh>
#include <ftk/filters/tracker.hh>
#include <ftk/filters/feature_point.hh>
#include <ftk/filters/feature_surface.hh>
#include <ftk/filters/feature_volume.hh>
#include <ftk/geometry/points2vtk.hh>
#include <ftk/geometry/cc2curves.hh>
#include <ftk/external/diy/serialization.hpp>
#include <ftk/external/diy-ext/gather.hh>
#include <ftk/io/tdgl.hh>
#include <iomanip>

namespace ftk {
  
struct xgc_blob_filament_tracker : public virtual tracker {
  xgc_blob_filament_tracker(diy::mpi::communicator comm, 
      const simplicial_unstructured_2d_mesh<>& m2, 
      int nphi_, int iphi_);

  int cpdims() const { return 3; }
 
  void set_nphi_iphi(int nphi, int iphi);

  void initialize() {}
  void reset() {
    field_data_snapshots.clear();
  }
  void update() {}
  void finalize() {}
  
public:
  void update_timestep();
  bool advance_timestep();

public:
  void push_field_data_snapshot(const ndarray<double> &scalar);
  void push_field_data_snapshot(
      const ndarray<double> &scalar, 
      const ndarray<double> &vector,
      const ndarray<double> &jacobian);
  
  bool pop_field_data_snapshot();
 
protected:
  bool check_simplex(int, feature_point_t& cp);
 
  template <int n, typename T>
  void simplex_values(
      const int i,
      int verts[n],
      int t[n],
      int p[n],
      T rz[n][2], 
      T f[n], // scalars
      T v[n][2], // vectors
      T j[n][2][2]); // jacobians

protected:
  struct field_data_snapshot_t {
    ndarray<double> scalar, vector, jacobian;
  };
  std::deque<field_data_snapshot_t> field_data_snapshots;

  const int nphi, iphi;
  const simplicial_unstructured_2d_mesh<>& m2;
  const simplicial_unstructured_3d_mesh<> m3;
  const simplicial_unstructured_extruded_3d_mesh<> m4;
  
protected:
  std::map<int, feature_point_t> discrete_critical_points;
};

/////
  
xgc_blob_filament_tracker::xgc_blob_filament_tracker(
    diy::mpi::communicator comm, 
    const simplicial_unstructured_2d_mesh<>& m2_, 
    int nphi_, int iphi_) :
  tracker(comm),
  m2(m2_), nphi(nphi_), iphi(iphi_),
  m3(ftk::simplicial_unstructured_3d_mesh<>::from_xgc_mesh(m2_, nphi_, iphi_)),
  m4(m3)
{

}

inline bool xgc_blob_filament_tracker::advance_timestep()
{
  update_timestep();
  pop_field_data_snapshot();

  current_timestep ++;
  return field_data_snapshots.size() > 0;
}

inline bool xgc_blob_filament_tracker::pop_field_data_snapshot()
{
  if (field_data_snapshots.size() > 0) {
    field_data_snapshots.pop_front();
    return true;
  } else return false;
}

inline void xgc_blob_filament_tracker::push_field_data_snapshot(
      const ndarray<double> &scalar)
{
  ndarray<double> F, G, J;

  F.reshape(scalar);
  G.reshape(2, scalar.dim(0), scalar.dim(1));
  J.reshape(2, 2, scalar.dim(0), scalar.dim(1));
  for (size_t i = 0; i < nphi; i ++) {
    ftk::ndarray<double> f, grad, j;
    auto slice = scalar.slice_time(i);
    m2.smooth_scalar_gradient_jacobian(slice, 0.03/*FIXME*/, f, grad, j);
    for (size_t k = 0; k < m2.n(0); k ++) {
      F(k, i) = f(k);
      G(0, k, i) = grad(0, k);
      G(1, k, i) = grad(1, k);
      J(0, 0, k, i) = j(0, 0, k);
      J(1, 0, k, i) = j(1, 0, k);
      J(1, 1, k, i) = j(1, 1, k);
      J(0, 1, k, i) = j(0, 1, k);
    }
  }

  push_field_data_snapshot(F, G, J);
}
  
inline void xgc_blob_filament_tracker::push_field_data_snapshot(
      const ndarray<double> &scalar, 
      const ndarray<double> &vector,
      const ndarray<double> &jacobian)
{
  field_data_snapshot_t snapshot;
  snapshot.scalar = scalar;
  snapshot.vector = vector;
  snapshot.jacobian = jacobian;

  field_data_snapshots.emplace_back(snapshot);
}

inline void xgc_blob_filament_tracker::update_timestep()
{
  if (comm.rank() == 0) fprintf(stderr, "current_timestep=%d\n", current_timestep);

  auto func = [&](int i) {
    feature_point_t cp;
    if (check_simplex(i, cp)) {
      std::lock_guard<std::mutex> guard(mutex);
        discrete_critical_points[i] = cp;
    }
  };

  m4.element_for_ordinal(2, current_timestep, func, xl, nthreads, enable_set_affinity);
  if (field_data_snapshots.size() >= 2)
    m4.element_for_interval(2, current_timestep, func, xl, nthreads, enable_set_affinity);
}

inline bool xgc_blob_filament_tracker::check_simplex(int i, feature_point_t& cp)
{
  int tri[3], t[3], p[3];
  double rz[3][2], f[3], v[3][2], j[3][2][2];
  long long vf[3][2];
  const long long factor = 1 << 15; // WIP

  simplex_values<3, double>(i, tri, t, p, rz, f, v, j);
  // print3x2("rz", rz);
  // print3x2("v", v);

  return false;
  for (int k = 0; k < 3; k ++) 
    for (int l = 0; l < 2; l ++) 
      vf[k][l] = factor * v[k][l];

  bool succ = ftk::robust_critical_point_in_simplex2(vf, tri);
  if (succ) {
    fprintf(stderr, "succ\n");
    return true;
  }

  return false;
}

template<int n, typename T>
void xgc_blob_filament_tracker::simplex_values(
    const int i, 
    int verts[n],
    int t[n], // time
    int p[n], // poloidal
    T rz[n][2], 
    T f[n],
    T v[n][2],
    T j[n][2][2])
{
  m4.get_simplex(n, i, verts);
  for (int k = 0; k < n; k ++) {
    t[k] = m4.flat_vertex_time(verts[k]);
    const int v3 = verts[k] % m3.n(0);
    const int v2 = v3 % m2.n(0);
    p[k] = v3 / m2.n(0); // poloidal plane

    m2.get_coords(v2, rz[k]);

    const int iv = (t[k] == current_timestep) ? 0 : 1; 
    const auto &data = field_data_snapshots[iv];
#if 0
    f[k] = data.scalar[v3];

    v[k][0] = data.vector[v3*2];
    v[k][0] = data.vector[v3*2+1];

    j[k][0][0] = data.jacobian[v3*4];
    j[k][0][1] = data.jacobian[v3*4+1];
    j[k][1][0] = data.jacobian[v3*4+2];
    j[k][1][1] = data.jacobian[v3*4+3];
#endif
  }
}

}

#endif