#ifndef _FTK_PARTICLE_TRACER_MPAS_OCEAN_HH
#define _FTK_PARTICLE_TRACER_MPAS_OCEAN_HH

#include <ftk/ndarray.hh>
#include <ftk/mesh/lattice_partitioner.hh>
#include <ftk/filters/particle_tracer.hh>
#include <ftk/filters/mpas_ocean_tracker.hh>
#include <ftk/utils/gather.hh>
#include <ftk/numeric/wachspress_interpolation.hh>
#include <ftk/numeric/vector_norm.hh>

namespace ftk {

struct particle_tracer_mpas_ocean : public particle_tracer, public mpas_ocean_tracker
{
  particle_tracer_mpas_ocean(diy::mpi::communicator comm, 
      std::shared_ptr<mpas_mesh<>> m) : 
    particle_tracer(comm, 3), 
    mpas_ocean_tracker(comm, m), 
    tracker(comm) 
  {
    this->integrator = PARTICLE_TRACER_INTEGRATOR_SPHERICAL_RK1;
  }

  virtual ~particle_tracer_mpas_ocean() {}

  void initialize_particles_at_grid_points(std::vector<int> strides);

  static constexpr double earth_radius = 6371229.0;

protected:
  bool eval_v(std::shared_ptr<ndarray<double>> V,
      const double* x, double *v);
  
  bool eval_v_vertical(std::shared_ptr<ndarray<double>> V,
      const double* x, double *v);
};

////
inline void particle_tracer_mpas_ocean::initialize_particles_at_grid_points(std::vector<int> strides)
{
  int stride_horizontal = 1, stride_vertical = 1;

  if (strides.size() >= 1)
    stride_horizontal = strides[0];

  if (strides.size() >= 2)
    stride_vertical = strides[1];

  // fprintf(stderr, "strides=%d, %d\n", stride_horizontal, stride_vertical);
  // fprintf(stderr, "ncells=%zu, nlayers=%zu\n", m->n_cells(), m->n_layers());

  for (auto i = 0; i < m->n_cells(); i += stride_horizontal) {
    double x0[3];
    for (auto k = 0; k < 3; k ++)
      x0[k] = m->xyzCells(k, i);
    const double R = vector_2norm<3>(x0);

    for (auto j = 0; j < m->n_layers(); j += stride_vertical) {
      const double thickness = m->accRestingThickness(j, i);
      const double r = R - thickness;
      for (auto k = 0; k < 3; k ++)
        x0[k] = x0[k] * r / R;

      feature_curve_t curve;
      feature_point_t p;
      for (auto k = 0; k < 3; k ++)
        p.x[k] = x0[k]; // m->xyzCells(k, i); 
     
      // fprintf(stderr, "cell=%zu, layer=%zu, thickness=%f, x0=%f, %f, %f\n", 
      //     i, j, thickness, x0[0], x0[1], x0[2]);
      curve.push_back(p);
      trajectories.add(curve);
    }
  }
  
  fprintf(stderr, "#trajectories=%zu\n", trajectories.size());
}

inline bool particle_tracer_mpas_ocean::eval_v_vertical(
    std::shared_ptr<ndarray<double>> V,
    const double *x, double *v)
{
  static const int max_nverts = 10, max_nlayers = 100;
 
  int cell_i = m->locate_cell_i(x);
  assert(cell_i < m->n_cells());

  int verts_i[max_nverts]; //  = {-1};
  const int nverts = m->verts_i_on_cell_i(cell_i, verts_i);
  const int nlayers = m->n_layers();

  double Xv[max_nverts][3]; // coordinates of vertices
  m->verts_i_coords(nverts, verts_i, Xv);

  double omega[max_nverts] = {0};
  wachspress_weights(nverts, Xv, x, omega);
  // with the wachpress weights, first interpolate the thickness
  // of each layer, and then find the proper layer

  double tops[max_nlayers] = {0};
  for (int l = 0; l < nlayers; l ++)
    for (int i = 0; i < nverts; i ++)
      tops[l] += V->at(3, l, verts_i[i]); // assuming channel 4 is zTop

  // locate depth layer
  const double z = vector_2norm<3>(x) - earth_radius;
  int i_layer = -1;
  for (int l = 0; l < nlayers; l ++)
    if (z < tops[l] && z > tops[l+1]) {
      i_layer = l;
      break;
    }

  if (i_layer < 0 || i_layer == nlayers - 1)
    return false; // vertical layer not found

  fprintf(stderr, "ilayer=%d\n", i_layer);

  double Vu[max_nverts][3], Vl[max_nverts][3]; // upper/lower velocities on vertices
  for (int i = 0; i < nverts; i ++)
    for (int k = 0; k < 3; k ++) {
      Vu[i][k] = V->at(k, i_layer, verts_i[i]);
      Vl[i][k] = V->at(k, i_layer+1, verts_i[i]);
    }
  
  double vu[3] = {0}, vl[3] = {0};
  for (int i = 0; i < nverts; i ++)
    for (int k = 0; k < 3; k ++) {
      vu[k] += omega[i] * Vu[i][k];
      vl[k] += omega[i] * Vl[i][k];
    }

  const double alpha = (z - tops[i_layer+1]) / (tops[i_layer] - tops[i_layer+1]);

  for (int k = 0; k < 3; k ++)
    v[k] = alpha * vu[k] + (1.0 - alpha) * vl[k];

  return true;
}


inline bool particle_tracer_mpas_ocean::eval_v(
    std::shared_ptr<ndarray<double>> V,
    const double *x, double *v)
{
  // 1. find the cell and number of vertices
  // 2. apply wachpress interpolation
  
  int cell_i = m->locate_cell_i(x);
  assert(cell_i < m->n_cells());

  static const int max_nverts = 10;
  int verts_i[max_nverts]; //  = {-1};
  const int nverts = m->verts_i_on_cell_i(cell_i, verts_i);

  // fprintf (stderr, "nverts=%d, verts=%d, %d, %d, %d, %d, %d, %d\n", 
  //     nverts, verts_i[0], verts_i[1], verts_i[2], verts_i[3], verts_i[4], verts_i[5], verts_i[6]);

  double Xv[max_nverts][3]; // coordinates of vertices
  m->verts_i_coords(nverts, verts_i, Xv);

  // for (int i = 0; i < nverts; i ++) 
  //   fprintf(stderr, "x%d=%f, %f, %f\n", i, Xv[i][0], Xv[i][1], Xv[i][2]);

  // std::cerr << V->shape() << std::endl;

  double Vv[max_nverts][3]; // velocities on vertices
  for (int i = 0; i < nverts; i ++)
    for (int k = 0; k < 3; k ++)
      Vv[i][k] = V->at(k, 0, verts_i[i]);
  
  // for (int i = 0; i < nverts; i ++) 
  //   fprintf(stderr, "v%d=%f, %f, %f\n", i, Vv[i][0], Vv[i][1], Vv[i][2]);

  double omega[max_nverts] = {0};
  wachspress_weights(nverts, Xv, x, omega);

  // fprintf(stderr, "omega=%f, %f, %f, %f, %f, %f, %f\n", 
  //     omega[0], omega[1], omega[2], omega[3], omega[4], omega[5], omega[6]);

  v[0] = v[1] = v[2] = 0.0;
  for (int i = 0; i < nverts; i ++)
    for (int k = 0; k < 3; k ++)
      v[k] += omega[i] * Vv[i][k]; // meters per second

  // v[3] = 1.0;

  // fprintf(stderr, "x=%f, %f, %f, %f, cell_i=%d, v=%f, %f, %f\n", 
  //     x[0], x[1], x[2], x[3], cell_i, v[0], v[1], v[2]);

#if 0
  for (size_t k = 0; k < 3; k ++)
    v[k] = V->get(k, 0, cellId) * 1e5;
#endif
  
  // fprintf(stderr, "x=%f, %f, %f, %f, cell_i=%zu, v=%f, %f, %f\n", 
  //     x[0], x[1], x[2], x[3], cell_i, v[0], v[1], v[2]);

  return true;
}

}

#endif