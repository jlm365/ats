/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
   ATS

   License: see $ATS_DIR/COPYRIGHT
   Author: Ethan Coon

   Interface for a StrongMPC which uses a preconditioner in which the
   block-diagonal cell-local matrix is dense.  If the system looks something
   like:

   A( y1, y2, x, t ) = 0
   B( y1, y2, x, t ) = 0

   where y1,y2 are spatially varying unknowns that are discretized using the MFD
   method (and therefore have both cell and face unknowns), an approximation to
   the Jacobian is written as

   [  dA_c/dy1_c  dA_c/dy1_f   dA_c/dy2_c       0      ]
   [  dA_f/dy1_c  dA_f/dy1_f      0              0      ]
   [  dB_c/dy1_c     0          dB_c/dy2_c  dB_c/dy2_f ]
   [      0           0          dB_f/dy2_c  dB_f/dy2_f ]


   Note that the upper left block is the standard preconditioner for the A
   system, and the lower right block is the standard precon for the B system,
   and we have simply added cell-based couplings, dA_c/dy2_c and dB_c/dy1_c.

   In the temperature/pressure system, these correspond to d_water_content /
   d_temperature and d_energy / d_pressure.

   ------------------------------------------------------------------------- */

#include <fstream>
#include "EpetraExt_RowMatrixOut.h"

#include "LinearOperatorFactory.hh"
#include "FieldEvaluator.hh"
#include "MatrixMFD.hh"
#include "MatrixMFD_Coupled.hh"

#include "mpc_coupled_cells.hh"

namespace Amanzi {

MPCCoupledCells::MPCCoupledCells(Teuchos::ParameterList& plist,
        const Teuchos::RCP<TreeVector>& soln) :
    PKDefaultBase(plist,soln),
    StrongMPC<PKPhysicalBDFBase>(plist,soln),
    decoupled_(false)
{}

void MPCCoupledCells::setup(const Teuchos::Ptr<State>& S) {
  StrongMPC<PKPhysicalBDFBase>::setup(S);

  decoupled_ = plist_.get<bool>("decoupled",false);

  A_key_ = plist_.get<std::string>("conserved quantity A");
  B_key_ = plist_.get<std::string>("conserved quantity B");
  y1_key_ = plist_.get<std::string>("primary variable A");
  y2_key_ = plist_.get<std::string>("primary variable B");
  dA_dy2_key_ = std::string("d")+A_key_+std::string("_d")+y2_key_;
  dB_dy1_key_ = std::string("d")+B_key_+std::string("_d")+y1_key_;

  Key mesh_key = plist_.get<std::string>("mesh key");
  mesh_ = S->GetMesh(mesh_key);

  // cells to debug
  if (plist_.isParameter("debug cells")) {
    dc_.clear();
    Teuchos::Array<int> dc = plist_.get<Teuchos::Array<int> >("debug cells");
    for (Teuchos::Array<int>::const_iterator c=dc.begin();
         c!=dc.end(); ++c) dc_.push_back(*c);

    // Enable a vo for each cell, allows parallel printing of debug cells.
    if (plist_.isParameter("debug cell ranks")) {
      Teuchos::Array<int> dc_ranks = plist_.get<Teuchos::Array<int> >("debug cell ranks");
      if (dc.size() != dc_ranks.size()) {
        Errors::Message message("Debug cell and debug cell ranks must be equal length.");
        Exceptions::amanzi_throw(message);
      }
      for (Teuchos::Array<int>::const_iterator dcr=dc_ranks.begin();
           dcr!=dc_ranks.end(); ++dcr) {
        // make a verbose object for each case
        Teuchos::ParameterList vo_plist;
        vo_plist.sublist("VerboseObject");
        vo_plist.sublist("VerboseObject")
            = plist_.sublist("VerboseObject");
        vo_plist.sublist("VerboseObject").set("write on rank", *dcr);

        dcvo_.push_back(Teuchos::rcp(new VerboseObject(mesh_->get_comm(), name_,vo_plist)));

      }
    } else {
      // Simply use the pk's vo
      dcvo_.resize(dc_.size(), vo_);
    }
  }

  // Create the precon
  Teuchos::ParameterList pc_sublist = plist_.sublist("Coupled PC");
  mfd_preconditioner_ =
      Teuchos::rcp(new Operators::MatrixMFD_Coupled(pc_sublist, mesh_));

  // Set the sub-blocks from the sub-PK's preconditioners.
  Teuchos::RCP<Operators::MatrixMFD> pcA = sub_pks_[0]->preconditioner();
  Teuchos::RCP<Operators::MatrixMFD> pcB = sub_pks_[1]->preconditioner();
  mfd_preconditioner_->SetSubBlocks(pcA, pcB);

  // setup and initialize the preconditioner
  mfd_preconditioner_->SymbolicAssembleGlobalMatrices();
  mfd_preconditioner_->InitPreconditioner();

  // setup and initialize the linear solver for the preconditioner
  if (plist_.isSublist("Coupled Solver")) {
    Teuchos::ParameterList linsolve_sublist = plist_.sublist("Coupled Solver");
    AmanziSolvers::LinearOperatorFactory<TreeMatrix,TreeVector,TreeVectorSpace> fac;
    linsolve_preconditioner_ = fac.Create("coupled solver", linsolve_sublist, mfd_preconditioner_);
  } else {
    linsolve_preconditioner_ = mfd_preconditioner_;
  }
}


// updates the preconditioner
void MPCCoupledCells::update_precon(double t, Teuchos::RCP<const TreeVector> up,
        double h) {
  StrongMPC<PKPhysicalBDFBase>::update_precon(t,up,h);

  // Update and get the off-diagonal terms.
  if (!decoupled_) {
    S_next_->GetFieldEvaluator(A_key_)
        ->HasFieldDerivativeChanged(S_next_.ptr(), name_, y2_key_);
    S_next_->GetFieldEvaluator(B_key_)
        ->HasFieldDerivativeChanged(S_next_.ptr(), name_, y1_key_);
    Teuchos::RCP<const CompositeVector> dA_dy2 = S_next_->GetFieldData(dA_dy2_key_);
    Teuchos::RCP<const CompositeVector> dB_dy1 = S_next_->GetFieldData(dB_dy1_key_);

    // collect derivatives
    Teuchos::RCP<Epetra_MultiVector> Ccc =
        Teuchos::rcp(new Epetra_MultiVector(*dA_dy2->ViewComponent("cell",false)));
    (*Ccc) = *dA_dy2->ViewComponent("cell",false);

    Teuchos::RCP<Epetra_MultiVector> Dcc =
        Teuchos::rcp(new Epetra_MultiVector(*dB_dy1->ViewComponent("cell",false)));
    (*Dcc) = *dB_dy1->ViewComponent("cell",false);

    if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {
      const Epetra_MultiVector& dsi_dp = *S_next_->GetFieldData("dsaturation_ice_dpressure")->ViewComponent("cell",false);
      const Epetra_MultiVector& dsi_dT = *S_next_->GetFieldData("dsaturation_ice_dtemperature")->ViewComponent("cell",false);

      for (std::vector<AmanziMesh::Entity_ID>::const_iterator c0=dc_.begin();
           c0!=dc_.end(); ++c0) {
        *out_ << "    dwc_dT(" << *c0 << "): " << (*Ccc)[0][*c0] << std::endl;
        *out_ << "    de_dp(" << *c0 << "): " << (*Dcc)[0][*c0] << std::endl;
        *out_ << "       dsi_dp(" << *c0 << "): " << dsi_dp[0][*c0] << std::endl;
        *out_ << "       dsi_dT(" << *c0 << "): " << dsi_dT[0][*c0] << std::endl;
        *out_ << "    --" << std::endl;

      }
    }

    // scale by 1/h
    Ccc->Scale(1./h);
    Dcc->Scale(1./h);
    mfd_preconditioner_->SetOffDiagonals(Ccc,Dcc);

    // compute
    if (std::abs(S_next_->time() - 3.04746e+07) < 68690.3) {
    //    if (std::abs(S_next_->time() - 3.03337e+07) < 68690.3) {
      std::cout << "DUMPING SCHUR!" << std::endl;
      mfd_preconditioner_->ComputeSchurComplement(true);
    } else {
      mfd_preconditioner_->ComputeSchurComplement();
    }

    // Assemble the precon, form Schur complement
    mfd_preconditioner_->UpdatePreconditioner();
  }
}


// applies preconditioner to u and returns the result in Pu
void MPCCoupledCells::precon(Teuchos::RCP<const TreeVector> u, Teuchos::RCP<TreeVector> Pu) {
  Teuchos::OSTab tab = getOSTab();

  if (decoupled_) return StrongMPC<PKPhysicalBDFBase>::precon(u,Pu);

  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_HIGH, true)) {
    for (std::vector<AmanziMesh::Entity_ID>::const_iterator c0=dc_.begin(); c0!=dc_.end(); ++c0) {
      AmanziMesh::Entity_ID_List fnums0;
      std::vector<int> dirs;
      mesh_->cell_get_faces_and_dirs(*c0, &fnums0, &dirs);

      *out_ << "Residuals:" << std::endl;
      *out_ << "  p(" << *c0 << "): " << (*u->SubVector(0)->Data())("cell",*c0);
      for (unsigned int n=0; n!=fnums0.size(); ++n) {
        *out_ << ",  " << (*u->SubVector(0)->Data())("face",fnums0[n]);
      }
      *out_ << std::endl;

      *out_ << "  T(" << *c0 << "): " << (*u->SubVector(1)->Data())("cell",*c0);
      for (unsigned int n=0; n!=fnums0.size(); ++n) {
        *out_ << ",  " << (*u->SubVector(1)->Data())("face",fnums0[n]);
      }
      *out_ << std::endl;
    }
  }

  // Apply
  linsolve_preconditioner_->ApplyInverse(*u, *Pu);

  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_HIGH, true)) {
    for (std::vector<AmanziMesh::Entity_ID>::const_iterator c0=dc_.begin(); c0!=dc_.end(); ++c0) {
      AmanziMesh::Entity_ID_List fnums0;
      std::vector<int> dirs;
      mesh_->cell_get_faces_and_dirs(*c0, &fnums0, &dirs);

      *out_ << "Preconditioned Updates:" << std::endl;
      *out_ << "  Pp(" << *c0 << "): " << (*Pu->SubVector(0)->Data())("cell",*c0);
      for (unsigned int n=0; n!=fnums0.size(); ++n) {
        *out_ << ",  " << (*Pu->SubVector(0)->Data())("face",fnums0[n]);
      }
      *out_ << std::endl;

      *out_ << "  PT(" << *c0 << "): " << (*Pu->SubVector(1)->Data())("cell",*c0);
      for (unsigned int n=0; n!=fnums0.size(); ++n) {
        *out_ << ",  " << (*Pu->SubVector(1)->Data())("face",fnums0[n]);
      }
      *out_ << std::endl;
    }
  }
}


} //  namespace
