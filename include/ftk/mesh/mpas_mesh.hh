#ifndef _FTK_MPAS_2D_MESH_HH
#define _FTK_MPAS_2D_MESH_HH

#include <ftk/config.hh>
#include <ftk/basic/kd.hh>
#include <ftk/mesh/simplicial_unstructured_2d_mesh.hh>
#include <ftk/numeric/mpas.hh>

namespace ftk {

template <typename I=int, typename F=double>
struct mpas_mesh { // : public simplicial_unstructured_2d_mesh<I, F> {
  void read_netcdf(const std::string filename, diy::mpi::communicator comm = MPI_COMM_WORLD);
  void initialize();

  void initialize_c2v_interpolants();
  ndarray<F> interpolate_c2v(const ndarray<F>& Vc) const; // velocites or whatever variables

  void initialize_edge_normals();
  void initialize_cell_tangent_plane();
  void initialize_coeffs_reconstruct();

#if FTK_HAVE_VTK
  vtkSmartPointer<vtkUnstructuredGrid> surface_cells_to_vtu(std::vector<ndarray_base*> attrs = {}) const;
#endif
  void surface_cells_to_vtu(const std::string filename, std::vector<ndarray_base*> attrs = {}) const;

public:
  size_t n_vertices() const { return xyzVertices.dim(1); }
  size_t n_edges() const { return xyzEdges.dim(1); }
  size_t n_cells() const { return xyzCells.dim(1); }
  size_t n_layers() const { return restingThickness.dim(0); }
  size_t max_edges_on_cell() const { return verticesOnCell.dim(0); }

  I locate_cell_i(const F x[]) const;
  I locate_cell_i(const F x[], const I prev_cell_i) const;
  I locate_cell_i(const std::array<F, 3>& x, const I prev) const { const F y[3] = {x[0], x[1], x[2]}; return locate_cell_i(y, prev); }

  void cell_i_coords(const I cell_i, F x[3]) const { x[0] = xyzCells(0, cell_i); x[1] = xyzCells(1, cell_i); x[2] = xyzCells(2, cell_i); }

  bool point_in_cell_i(const I cell_i, const F x[]) const;
  bool point_in_cell(const int nverts, const F Xv[][3], const F x[3]) const;

  // void interpolate_c2v(const I vid, const F cvals[3][], F vvals[]) const;

  I cid2i(I cid) const { 
    if (simple_idmap) return cid - 1;
    else {
      auto it = cellIdToIndex.find(cid); 
      if (it != cellIdToIndex.end()) return it->second; 
      else return -1;  
    }
  }

  I eid2i(I eid) const {
    if (simple_idmap) return eid - 1;
    else {
      auto it = edgeIdToIndex.find(eid); 
      if (it != edgeIdToIndex.end()) return it->second; 
      else return -1;  
    }
  }
  
  I vid2i(I vid) const {
    if (simple_idmap) return vid - 1; 
    else {
      auto it = vertexIdToIndex.find(vid); 
      if (it != vertexIdToIndex.end()) return it->second; 
      else return -1; 
    }
  }

  I i2cid(I i) const { if (simple_idmap) return i+1; else return indexToCellID[i]; }
  I i2vid(I i) const { if (simple_idmap) return i+1; return indexToVertexID[i]; }

  I verts_i_on_cell_i(const I i, I vi[]) const; // returns number of verts
  void vert_i_coords(const I i, F X[3]) const;
  void verts_i_coords(const I n, const I i[], F X[][3]) const;

  bool is_vertex_on_boundary(I vid) const { return vertex_on_boundary[vertex_id_to_index(vid)]; }

public:
  std::shared_ptr<kd_t<F, 3>> kd_cells, kd_vertices;
  
  ndarray<I> cellsOnVertex, cellsOnEdge, cellsOnCell,
             edgesOnCell,
             verticesOnCell;
  ndarray<I> indexToVertexID, indexToEdgeID, indexToCellID;
  ndarray<F> xyzCells, xyzVertices, xyzEdges;
  ndarray<I> nEdgesOnCell;

  ndarray<F> edgeNormalVectors;
  ndarray<F> cellTangentPlane;
  ndarray<F> coeffsReconstruct; // coeffs for edge->cell interpolation

  ndarray<F> c2v_interpolants;
  std::vector<bool> vertex_on_boundary;

  std::map<I, I> cellIdToIndex, // cellID->index
                 edgeIdToIndex,              
                 vertexIdToIndex; // vertexID->index
  
  // ndarray<I> cells_i_on_cell_i, verts_i_on_cell_i;

  ndarray<F> restingThickness, // Layer thickness when the ocean is at rest, i.e. without SSH or internal perturbations
             accRestingThickness;

  bool simple_idmap = false;

#if 0
  ndarray<I> voronoi_c2v, // (6, nc)
             voronoi_v2c; // (3, nv)
  ndarray<F> voronoi_vcoords; // (3, nv)
#endif
};

//////
template <typename I, typename F>
void mpas_mesh<I, F>::initialize()
{
  kd_cells.reset(new kd_t<F, 3>);
  kd_cells->set_inputs(this->xyzCells);
  kd_cells->build();
}

template <typename I, typename F>
void mpas_mesh<I, F>::initialize_c2v_interpolants()
{
  c2v_interpolants.reshape(3, n_vertices());
  vertex_on_boundary.resize(n_vertices());

  for (I i = 0; i < n_vertices(); i ++) {
    F Xc[3][3], Xv[3];
   
    bool boundary = false;
    for (I j = 0; j < 3; j ++) {
      const I c = cid2i( cellsOnVertex(j, i) );
      if (c < 0) {
        boundary = true;
        break;
      }
      // fprintf(stderr, "cell=%d\n", c);
      Xv[j] = xyzVertices(j, i);
      for (I k = 0; k < 3; k ++)
        Xc[j][k] = xyzCells(k, c);
    }

    if (boundary) {
      vertex_on_boundary[i] = true;
      continue;
    }

    // print3x3("Xc", Xc);
    // print3("Xv", Xv);
      
    F mu[3];
    bool succ = inverse_lerp_s2v3_0(Xc, Xv, mu);
    for (I k = 0; k < 3; k ++)
      c2v_interpolants(k, i) = mu[k];

    // fprintf(stderr, "mu=%f, %f, %f\n", 
    //     mu[0], mu[1], mu[2]);
  }
}

template <typename I, typename F>
ndarray<F> mpas_mesh<I, F>::interpolate_c2v(const ndarray<F>& Vc) const
{
  const I nvars = Vc.dim(0);

  ndarray<F> V; // variables on vertices
  V.reshape(nvars, Vc.dim(1), n_vertices());

  for (auto i = 0; i < n_vertices(); i ++) {
    const auto vid = i2vid(i);
    // bool boundary = is_vertex_on_boundary(vid);
    const bool boundary = vertex_on_boundary[i];

    F lambda[3] = {0};
    I c[3] = {0};
    if (!boundary) {
      for (auto k = 0; k < 3; k ++) {
        lambda[k] = c2v_interpolants(k, i);
        c[k] = cid2i( cellsOnVertex(k, i) );
      }
    }

    for (auto layer = 0; layer < Vc.dim(1); layer ++) {
      for (auto k = 0; k < nvars; k ++) {
        if (boundary) {
          V(k, layer, i) = F(0);
        } else {
          bool invalid = false;
          for (auto l = 0; l < 3; l ++) {
            F val = Vc(k, layer, c[l]);
            if (val < -1e33) {
              invalid = true;
              break;
            }
            V(k, layer, i) += lambda[l] * Vc(k, layer, c[l]);
          }

          if (invalid)
            V(k, layer, i) = std::nan("1");
        }
      }
    }
  }

  return V;
}

// inline mpas_mesh::mpas_mesh(const mpas_mesh& mm) :

template <typename I, typename F>
void mpas_mesh<I, F>::vert_i_coords(const I i, F X[3]) const
{
  for (int k = 0; k < 3; k ++)
    X[k] = xyzVertices(k, i);
}

template <typename I, typename F>
void mpas_mesh<I, F>::verts_i_coords(const I n, const I i[], F X[][3]) const
{
  for (auto j = 0; j < n; j ++)
    vert_i_coords(i[j], X[j]);
}

template <typename I, typename F>
I mpas_mesh<I, F>::verts_i_on_cell_i(const I i, I vi[]) const
{
  I n = nEdgesOnCell[i];
  for (auto j = 0; j < n; j ++)
    vi[j] = vid2i( verticesOnCell(j, i) );
  return n;
}

template <typename I, typename F>
void mpas_mesh<I, F>::initialize_edge_normals()
{
  edgeNormalVectors.reshape(xyzEdges);

  F n[3];
  for (auto ei = 0; ei < n_edges(); ei ++) {
    const I ci1 = cid2i( cellsOnEdge(0, ei) ),
            ci2 = cid2i( cellsOnEdge(1, ei) );

    for (int i = 0; i < 3; i ++) {
      if (ci1 < 0) // boundary edge case I
        n[i] = xyzCells(i, ci2) - xyzEdges(i, ei);
      else if (ci2 < 0) // boundary edge case II
        n[i] = xyzEdges(i, ei) - xyzCells(i, ci1);
      else
        n[i] = xyzCells(i, ci2) - xyzCells(i, ci1);
    }

    vector_normalization2_3(n);
    for (int i = 0; i < 3; i ++)
      edgeNormalVectors[i] = n[i];
  }
}

template <typename I, typename F>
void mpas_mesh<I, F>::initialize_cell_tangent_plane()
{
  if (edgeNormalVectors.empty())
    initialize_edge_normals();

  cellTangentPlane.reshape(3, 2, n_cells());
  F p[2], // tangent plane
    rhat[3], // unit vector for a cell,
    xhat[3],
    yhat[3],
    n[3]; // edge normal of the first edge

  for (auto ci = 0; ci < n_cells(); ci ++) {
    cell_i_coords(ci, rhat);
    vector_normalization2_3(rhat);

    const I ei = edgesOnCell(0, ci);
    for (int i = 0; i < 3; i ++)
      n[i] = edgeNormalVectors(i, ei);

    const F normal_dot_r = vector_dot_product3(rhat, n);

    for (int i = 0; i < 3; i ++)
      xhat[i] = n[i] - normal_dot_r * rhat[i];
    vector_normalization2_3(xhat);

    cross_product(rhat, xhat, yhat);
    vector_normalization2_3(yhat); // not necesary but just make sure
 
    for (int i = 0; i < 3; i ++) {
      cellTangentPlane(i, 0, ci) = xhat[i];
      cellTangentPlane(i, 1, ci) = yhat[i];
    }
  }
}

template <typename I, typename F>
void mpas_mesh<I, F>::initialize_coeffs_reconstruct()
{
  if (cellTangentPlane.empty())
    initialize_cell_tangent_plane();

  const auto max_edges = max_edges_on_cell();
  coeffsReconstruct.reshape(3, max_edges);

  for (auto ci = 0; ci < n_cells(); ci ++) {
    F x[3];
    cell_i_coords(ci, x);

    const auto ne = nEdgesOnCell[ci];
    F Xe[max_edges][3], Ne[max_edges][3];
    F alpha = 0;
    for (auto i = 0; i < ne; i ++) {
      const auto ei = eid2i( edgesOnCell(i, ci) );

      for (auto j = 0; j < 3; j ++) {
        Xe[i][j] = xyzEdges(j, ei);
        Ne[i][j] = edgeNormalVectors(j, ei);
      }

      alpha += vector_dist_2norm_3(x, Xe[i]);
    }

    const F t[2][3] = {
      {cellTangentPlane(0, 0, ci), cellTangentPlane(1, 0, ci), cellTangentPlane(2, 0, ci)},
      {cellTangentPlane(0, 1, ci), cellTangentPlane(1, 1, ci), cellTangentPlane(2, 1, ci)}
    };

    F coeffs[max_edges];
    rbf3d_plane_vec_const_dir(
        max_edges, Xe, Ne, x, alpha, t, coeffs);

    for (auto i = 0; i < ne; i ++)
      coeffsReconstruct(i, ci) = coeffs[i];
  }
}

template <typename I, typename F>
void mpas_mesh<I, F>::read_netcdf(const std::string filename, diy::mpi::communicator comm)
{
#if FTK_HAVE_NETCDF
  int ncid;
  NC_SAFE_CALL( nc_open(filename.c_str(), NC_NOWRITE, &ncid) );

  cellsOnVertex.read_netcdf(ncid, "cellsOnVertex"); // "conn"
  cellsOnEdge.read_netcdf(ncid, "cellsOnEdge");
  cellsOnCell.read_netcdf(ncid, "cellsOnCell");
  edgesOnCell.read_netcdf(ncid, "edgesOnCell");
  verticesOnCell.read_netcdf(ncid, "verticesOnCell");
  nEdgesOnCell.read_netcdf(ncid, "nEdgesOnCell");

  simple_idmap = true;

  indexToCellID.read_netcdf(ncid, "indexToCellID");
  for (auto i = 0; i < indexToCellID.size(); i ++) {
    cellIdToIndex[indexToCellID[i]] = i;
    if (indexToCellID[i] != i + 1)
      simple_idmap = false;
  }

  indexToEdgeID.read_netcdf(ncid, "indexToEdgeID");
  for (auto i = 0; i < indexToEdgeID.size(); i ++) {
    edgeIdToIndex[indexToEdgeID[i]] = i;
    if (indexToEdgeID[i] != i + 1) 
      simple_idmap = false;
  }
  
  indexToVertexID.read_netcdf(ncid, "indexToVertexID");
  for (auto i = 0; i < indexToVertexID.size(); i ++) {
    vertexIdToIndex[indexToVertexID[i]] = i;
    if (indexToVertexID[i] != i + 1)
      simple_idmap = false;
  }
  
  // fprintf(stderr, "simple_idmap=%d\n", simple_idmap);
  if (simple_idmap) {
    indexToVertexID.reset();
    indexToEdgeID.reset();
    indexToCellID.reset();

    cellIdToIndex.clear();
    edgeIdToIndex.clear();
    vertexIdToIndex.clear();
  }

#if 0
  std::vector<I> vconn;
  for (int i = 0; i < cellsOnVertex.dim(1); i ++) {
    if (cellsOnVertex(0, i) == 0 || 
        cellsOnVertex(1, i) == 0 ||
        cellsOnVertex(2, i) == 0) 
      continue;
    else {
      for (int j = 0; j < 3; j ++) 
        vconn.push_back(cellIdToIndex[cellsOnVertex(j, i)]);
    }
  }

  ndarray<I> conn;
  conn.copy_vector(vconn);
  conn.reshape(3, vconn.size()/3);
#endif

  // xyzCells
  ndarray<F> xCell, yCell, zCell;
  xCell.read_netcdf(ncid, "xCell");
  yCell.read_netcdf(ncid, "yCell");
  zCell.read_netcdf(ncid, "zCell");
  // latCell.from_netcdf(ncid, "latCell");
  // lonCell.from_netcdf(ncid, "lonCell");
  
  xyzCells.reshape(3, xCell.size());
  for (size_t i = 0; i < xCell.size(); i ++) {
    xyzCells(0, i) = xCell[i];
    xyzCells(1, i) = yCell[i];
    xyzCells(2, i) = zCell[i];
  }

  // xyzVerts
  ndarray<F> xVertex, yVertex, zVertex;
  xVertex.read_netcdf(ncid, "xVertex");
  yVertex.read_netcdf(ncid, "yVertex");
  zVertex.read_netcdf(ncid, "zVertex");

  xyzVertices.reshape(3, xVertex.size());
  for (size_t i = 0; i < xVertex.size(); i ++) {
    xyzVertices(0, i) = xVertex[i];
    xyzVertices(1, i) = yVertex[i];
    xyzVertices(2, i) = zVertex[i];
  }

  // xyzEdges
  ndarray<F> xEdge, yEdge, zEdge;
  xEdge.read_netcdf(ncid, "xEdge");
  yEdge.read_netcdf(ncid, "yEdge");
  zEdge.read_netcdf(ncid, "zEdge");

  xyzEdges.reshape(3, xEdge.size());
  for (size_t i = 0; i < xEdge.size(); i ++) {
    xyzEdges(0, i) = xEdge[i];
    xyzEdges(1, i) = yEdge[i];
    xyzEdges(2, i) = zEdge[i];
  }

  restingThickness.read_netcdf(ncid, "restingThickness");
  accRestingThickness.reshape(restingThickness);

  for (auto i = 0; i < restingThickness.dim(1); i ++) { // cells
    for (auto j = 0; j < restingThickness.dim(0); j ++) { // vertical layers
      if (j == 0) accRestingThickness(j, i) = 0;
      else accRestingThickness(j, i) = accRestingThickness(j-1, i) + restingThickness(j, i);
    }
  }

  NC_SAFE_CALL( nc_close(ncid) );

  // fprintf(stderr, "mpas mesh: #vert=%zu, #tri=%zu\n", 
  //     xyzCells.dim(1), conn.dim(1));

  // return std::shared_ptr<mpas_mesh<I, F>>(
  //     new mpas_mesh<I, F>(xyz, conn));
#else
  fatal(FTK_ERR_NOT_BUILT_WITH_NETCDF);
#endif
}

#if FTK_HAVE_VTK
template <typename I, typename F>
vtkSmartPointer<vtkUnstructuredGrid> mpas_mesh<I, F>::surface_cells_to_vtu(const std::vector<ndarray_base*> attrs) const
{
  vtkSmartPointer<vtkUnstructuredGrid> grid = vtkUnstructuredGrid::New();
  vtkSmartPointer<vtkPoints> pts = vtkPoints::New();
  pts->SetNumberOfPoints(n_vertices());

  for (auto i = 0; i < n_vertices(); i ++)
    pts->SetPoint(i, xyzVertices(0, i), xyzVertices(1, i), xyzVertices(2, i));
  grid->SetPoints(pts);

  for (auto i = 0; i < n_cells(); i ++) {
    vtkIdType ids[7];
    for (auto j = 0; j < 7; j ++) {
      auto id = vid2i( verticesOnCell(j, i) );
      ids[j] = id;
    }

    const int n = nEdgesOnCell[i];
    // fprintf(stderr, "%zu, %d\n", i, n);
      
    grid->InsertNextCell( VTK_POLYGON, n, ids );

#if 0
    if (n == 6)
      grid->InsertNextCell( VTK_HEXAHEDRON, n, ids );
    else if (n == 7 || n == 5) 
      grid->InsertNextCell( VTK_POLYGON, n, ids );
    else if (n == 4)
      grid->InsertNextCell( VTK_QUAD, n, ids );
#endif
  }

  for (const auto& arr : attrs) {
    auto data = arr->to_vtk_data_array("att");
    grid->GetPointData()->AddArray(data);
  }

  return grid;
}
#endif
  
template <typename I, typename F>
void mpas_mesh<I, F>::surface_cells_to_vtu(const std::string filename, const std::vector<ndarray_base*> attrs) const
{
#if FTK_HAVE_VTK
  auto grid = surface_cells_to_vtu(attrs);
  vtkSmartPointer<vtkXMLUnstructuredGridWriter> writer = vtkXMLUnstructuredGridWriter::New();
  writer->SetFileName( filename.c_str() );
  writer->SetInputData( grid );
  writer->Write();
#else
  fatal(FTK_ERR_NOT_BUILT_WITH_VTK);
#endif
}

template <typename I, typename F>
bool mpas_mesh<I, F>::point_in_cell_i(const I cell_i, const F x[]) const
{
  constexpr int max_nverts = 10;
  int verts_i[max_nverts];

  const int nverts = verts_i_on_cell_i(cell_i, verts_i);
  double Xv[max_nverts][3];
  
  verts_i_coords(nverts, verts_i, Xv);
  
  return point_in_mpas_cell<F>(nverts, Xv, x);
}

template <typename I, typename F>
I mpas_mesh<I, F>::locate_cell_i(const F x[]) const
{
  I cell_i = kd_cells->find_nearest(x);
  if (point_in_cell_i(cell_i, x))
    return cell_i;
  else
    return -1;
}

template <typename I, typename F>
I mpas_mesh<I, F>::locate_cell_i(const F x[], const I prev_cell_i) const
{
  if (prev_cell_i < 0) 
    return locate_cell_i(x);
  else {
    // among the neighbors, find the cell center with min dist to x
 
    F cx[3];
    cell_i_coords(prev_cell_i, cx);

    I mindist2_cell_i = prev_cell_i;
    F mindist2 = vector_dist_2norm2<3>(cx, x);

    const int n = nEdgesOnCell[prev_cell_i];
    for (int i = 0; i < n; i ++) {
      const I cell_i = cid2i( cellsOnCell(i, prev_cell_i) );
      if (cell_i < 0)
        continue;

      cell_i_coords(cell_i, cx);

      const F dist2 = vector_dist_2norm2<3>(cx, x);

      if (dist2 < mindist2) {
        mindist2_cell_i = cell_i;
        mindist2 = dist2;
      }
    }

#if 0
    if (prev_cell_i == mindist2_cell_i)
      fprintf(stderr, "same cell %d\n", prev_cell_i);
    else 
      fprintf(stderr, "different cell, prev=%d, current=%d\n", prev_cell_i, mindist2_cell_i);
#endif

    if (point_in_cell_i(mindist2_cell_i, x))
      return mindist2_cell_i;
    else {
      I cell_i = locate_cell_i(x);
      return cell_i;
#if 0
      fprintf(stderr, "x=%f, %f, %f is not in cell %d (neighor of %d), but is in %d\n", 
          x[0], x[1], x[2], 
          mindist2_cell_i, prev_cell_i, cell_i);
#endif
    }
  }
}

} // namespace ftk

#endif 
