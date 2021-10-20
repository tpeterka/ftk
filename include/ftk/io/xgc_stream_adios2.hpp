#ifndef _FTK_XGC_STREAM_ADIOS2_HH
#define _FTK_XGC_STREAM_ADIOS2_HH

namespace ftk {
using nlohmann::json;

struct xgc_stream_adios2 : public xgc_stream
{
  xgc_stream_adios2(const std::string& path, diy::mpi::communicator comm = MPI_COMM_WORLD) : xgc_stream(path, comm) {}
  
  std::string postfix() const { return ".bp"; }

  bool read_oneddiag();
  bool advance_timestep();
};

inline bool xgc_stream_adios2::read_oneddiag()
{
  const auto f = oneddiag_filename();
  ndarray<double> etemp_par, etemp_per;
  
  try {
    steps.read_bp(f, "step");
    time.read_bp(f, "time");

    try {
      etemp_par.read_bp(f, "e_parallel_mean_en_avg");
    } catch (...) {
      warn("cannot read e_parallel_mean_en_avg, use _df_1d instead");
      try {
        etemp_par.read_bp(f, "e_parallel_mean_en_df_1d");
      } catch (...) {
        warn("cannot read e_parallel_mean_en_df_1d");
        return false;
      }
    }

    try {
      etemp_per.read_bp(f, "e_perp_temperature_avg");
    } catch (...) {
      warn("cannot read e_prep_temperature_avg, use _df_1d instead");
      try {
        etemp_per.read_bp(f, "e_perp_temperature_df_1d");
      } catch (...) {
        warn("cannot read e_perp_temperature_df_1d");
        return false;
      }
    }

    Te1d = (etemp_par + etemp_per) * (2.0 / 3.0);
    return true;
  } catch (...) {
    warn("cannot read oneddiag file");
    return false;
  }
}

inline bool xgc_stream_adios2::advance_timestep()
{
  std::shared_ptr<ndarray_group> g(new ndarray_group);

  if (current_timestep >= start_timestep + ntimesteps)
    return false;

  const auto current_filename = filename( current_timestep );
  fprintf(stderr, "advancing.., %d, %d, %d, filename=%s\n", start_timestep, current_timestep, ntimesteps, current_filename.c_str());

  if (is_directory( current_filename )) {
    std::shared_ptr<ndarray_base> density( new ndarray<double> );
    density->read_bp(current_filename, "dpot");
    g->set("density", density);
    
    // std::shared_ptr<ndarray_base> Er( new ndarray<double> );
    // Er->read_adios2(current_filename, "Er");
    // g->set("Er", Er);
  
    callback(current_timestep, g);

    current_timestep ++;
    return true;
  } else return false;
}

}

#endif
