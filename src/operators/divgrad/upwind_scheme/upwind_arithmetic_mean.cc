/* -*-  mode: c++; indent-tabs-mode: nil -*- */

// -----------------------------------------------------------------------------
// ATS
//
// License: see $ATS_DIR/COPYRIGHT
// Author: Ethan Coon (ecoon@lanl.gov)
//
// Scheme for taking coefficients for div-grad operators from cells to
// faces.
// -----------------------------------------------------------------------------

#include "CompositeVector.hh"
#include "State.hh"
#include "upwind_arithmetic_mean.hh"

namespace Amanzi {
namespace Operators {

UpwindArithmeticMean::UpwindArithmeticMean(std::string pkname,
        std::string cell_coef,
        std::string face_coef) :
    pkname_(pkname),
    cell_coef_(cell_coef),
    face_coef_(face_coef) {};


void UpwindArithmeticMean::Update(const Teuchos::Ptr<State>& S,
                                  const Teuchos::Ptr<Debugger>& db) {
  Teuchos::RCP<const CompositeVector> cell = S->GetFieldData(cell_coef_);
  Teuchos::RCP<CompositeVector> face = S->GetFieldData(face_coef_, pkname_);
  CalculateCoefficientsOnFaces(*cell, face.ptr());
};


void UpwindArithmeticMean::CalculateCoefficientsOnFaces(
        const CompositeVector& cell_coef,
        const Teuchos::Ptr<CompositeVector>& face_coef) {

  Teuchos::RCP<const AmanziMesh::Mesh> mesh = face_coef->Mesh();
  AmanziMesh::Entity_ID_List faces;

  // initialize the face coefficients
  face_coef->ViewComponent("face",true)->PutScalar(0.0);
  if (face_coef->HasComponent("cell")) {
    face_coef->ViewComponent("cell",true)->PutScalar(1.0);
  }

  // Note that by scattering, and then looping over all USED cells, we
  // end up getting the correct upwind values in all faces (owned or
  // not) bordering an owned cell.  This is the necessary data for
  // making the local matrices in MFD, so there is no need to
  // communicate the resulting face coeficients.

  // communicate ghosted cells
  cell_coef.ScatterMasterToGhosted("cell");

  Epetra_MultiVector& face_coef_f = *face_coef->ViewComponent("face",true);
  const Epetra_MultiVector& cell_coef_c = *cell_coef.ViewComponent("cell",true);

  int c_used = cell_coef.size("cell", true);
  for (int c=0; c!=c_used; ++c) {
    mesh->cell_get_faces(c, &faces);

    for (unsigned int n=0; n!=faces.size(); ++n) {
      int f = faces[n];
      face_coef_f[0][f] += cell_coef_c[0][c] / 2.0;
    }
  }

  // rescale boundary faces, as these had only one cell neighbor
  unsigned int f_owned = mesh->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  for (unsigned int f=0; f!=f_owned; ++f) {
    AmanziMesh::Entity_ID_List cells;
    mesh->face_get_cells(f, AmanziMesh::USED, &cells);
    if (cells.size() == 1) {
      face_coef_f[0][f] *= 2.;
    }
  }
};

void
UpwindArithmeticMean::UpdateDerivatives(const Teuchos::Ptr<State>& S,
                                        std::string potential_key, 
                                        const CompositeVector& dconductivity,
                                        const std::vector<int>& bc_markers,
                                        const std::vector<double>& bc_values,
                                        std::vector<Teuchos::RCP<Teuchos::SerialDenseMatrix<int, double> > >* Jpp_faces) const {

  // Grab derivatives
  dconductivity.ScatterMasterToGhosted("cell");
  const Epetra_MultiVector& dcell_v = *dconductivity.ViewComponent("cell",true);

  // Grab potential
  Teuchos::RCP<const CompositeVector> pres = S->GetFieldData(potential_key);
  pres->ScatterMasterToGhosted("cell");
  const Epetra_MultiVector& pres_v = *pres->ViewComponent("cell",true);

  // Grab mesh and allocate space
  Teuchos::RCP<const AmanziMesh::Mesh> mesh = pres->Mesh();
  unsigned int nfaces_owned = mesh->num_entities(AmanziMesh::FACE,AmanziMesh::OWNED);
  Jpp_faces->resize(nfaces_owned);

  // workspace
  double dK_dp[2];
  double p[2];
  
  for (unsigned int f=0; f!=nfaces_owned; ++f) {
    // get neighboring cells
    AmanziMesh::Entity_ID_List cells;
    mesh->face_get_cells(f, AmanziMesh::USED, &cells);
    int mcells = cells.size();

    // create the local matrix
    Teuchos::RCP<Teuchos::SerialDenseMatrix<int, double> > Jpp =
      Teuchos::rcp(new Teuchos::SerialDenseMatrix<int, double>(mcells, mcells));
    (*Jpp_faces)[f] = Jpp;
    
    if (mcells == 1) {
      if (bc_markers[f] == Operators::OPERATOR_BC_DIRICHLET) {
        p[0] = pres_v[0][cells[0]];
        p[1] = bc_values[f];
        double dp = p[0] - p[1];

        (*Jpp)(0,0) = dp * mesh->face_area(f) * dcell_v[0][cells[0]];
      } else {
        (*Jpp)(0,0) = 0.;
      }
    } else {
      p[0] = pres_v[0][cells[0]];
      p[1] = pres_v[0][cells[1]];

      dK_dp[0] = 0.5 * dcell_v[0][cells[0]];
      dK_dp[1] = 0.5 * dcell_v[0][cells[1]];

      (*Jpp)(0,0) = (p[0] - p[1]) * mesh->face_area(f) * dK_dp[0];
      (*Jpp)(0,1) = (p[0] - p[1]) * mesh->face_area(f) * dK_dp[1];
      (*Jpp)(1,0) = -(*Jpp)(0,0);
      (*Jpp)(1,1) = -(*Jpp)(0,1);
    }
  }
}


} //namespace
} //namespace
