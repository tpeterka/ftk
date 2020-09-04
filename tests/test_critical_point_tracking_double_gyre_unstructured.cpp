#define CATCH_CONFIG_RUNNER
#include "catch.hh"
#include "constants.hh"
#include <ftk/filters/critical_point_tracker_wrapper.hh>
#include <ftk/filters/critical_point_tracker_2d_unstructured.hh>

using nlohmann::json;

const std::string mesh_filename = "2x1.vtu";
const int nt = 32;

#if FTK_HAVE_VTK
TEST_CASE("critical_point_tracking_double_gyre_unstructured") {
  diy::mpi::communicator world;
 
  ftk::simplicial_unstructured_2d_mesh<> m;
  m.from_vtk_unstructured_grid_file(mesh_filename);

  ftk::critical_point_tracker_2d_unstructured tracker(m);
  tracker.initialize();

  for (int i = 0; i < nt; i ++) {
    auto data = ftk::synthetic_double_gyre_unstructured<double>(
        m.get_coords(), static_cast<double>(i));
   
#if 0 // write back
    char filename[1024];
    sprintf(filename, "double-gyre-%02d.vtu", i);
    m.vector_to_vtk_unstructured_grid_data_file(filename, "vector", data);
#endif

    tracker.push_field_data_snapshot(
        ftk::ndarray<double>(), data, ftk::ndarray<double>());

    if (i != 0) tracker.advance_timestep();
    else tracker.update_timestep();
  }

  tracker.finalize();
  tracker.write_traced_critical_points_vtk("double_gyre_unstructured.vtp");

  auto trajs = tracker.get_traced_critical_points();
  
  if (world.rank() == 0)
    REQUIRE(trajs.size() == 5); 
}
#endif

int main(int argc, char **argv)
{
  diy::mpi::environment env;
  
  Catch::Session session;
  return session.run(argc, argv);
}
