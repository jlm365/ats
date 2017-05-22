/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/*
  Evaluates the porosity, given a small compressibility of rock.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#include "compressible_porosity_leijnse_evaluator.hh"
#include "compressible_porosity_leijnse_model.hh"

namespace Amanzi {
namespace Flow {
namespace FlowRelations {

CompressiblePorosityLeijnseEvaluator::CompressiblePorosityLeijnseEvaluator(Teuchos::ParameterList& plist) :
    SecondaryVariableFieldEvaluator(plist) {
  
  std::string domain_name=getDomain(my_key_);
  pres_key_ = plist_.get<std::string>("pressure key", getKey(domain_name, "pressure"));
  dependencies_.insert(pres_key_);
  
  poro_key_ = plist_.get<std::string>("base porosity key", getKey(domain_name,"base_porosity"));
  dependencies_.insert(poro_key_);

  if (my_key_ == std::string("")) {
    my_key_ = plist_.get<std::string>("porosity key",
                                      getKey(domain_name, "porosity"));
  }

  ASSERT(plist_.isSublist("compressible porosity model parameters"));
  models_ = createCompressiblePorosityLeijnseModelPartition(plist_.sublist("compressible porosity model parameters"));
}


CompressiblePorosityLeijnseEvaluator::CompressiblePorosityLeijnseEvaluator(const CompressiblePorosityLeijnseEvaluator& other) :
    SecondaryVariableFieldEvaluator(other),
    pres_key_(other.pres_key_),
    poro_key_(other.poro_key_),
    models_(other.models_) {}

Teuchos::RCP<FieldEvaluator>
CompressiblePorosityLeijnseEvaluator::Clone() const {
  return Teuchos::rcp(new CompressiblePorosityLeijnseEvaluator(*this));
}


// Required methods from SecondaryVariableFieldEvaluator
void CompressiblePorosityLeijnseEvaluator::EvaluateField_(const Teuchos::Ptr<State>& S,
        const Teuchos::Ptr<CompositeVector>& result) {
  // Initialize the MeshPartition
  if (!models_->first->initialized()) {
    models_->first->Initialize(result->Mesh(), -1);
    models_->first->Verify();
  }

  Teuchos::RCP<const CompositeVector> pres = S->GetFieldData(pres_key_);
  Teuchos::RCP<const CompositeVector> poro = S->GetFieldData(poro_key_);
  const double& patm = *S->GetScalarData("atmospheric_pressure");

  // evaluate the model
  for (CompositeVector::name_iterator comp=result->begin();
       comp!=result->end(); ++comp) {
    ASSERT(*comp == "cell");  // partition on cell only, could add boundary_face if needed (but not currently)
    const Epetra_MultiVector& pres_v = *(pres->ViewComponent(*comp,false));
    const Epetra_MultiVector& poro_v = *(poro->ViewComponent(*comp,false));
    Epetra_MultiVector& result_v = *(result->ViewComponent(*comp,false));

    int count = result->size(*comp);
    for (int id=0; id!=count; ++id) {
      result_v[0][id] = models_->second[(*models_->first)[id]]->Porosity(poro_v[0][id], pres_v[0][id], patm);
    }
  }
}


void CompressiblePorosityLeijnseEvaluator::EvaluateFieldPartialDerivative_(
    const Teuchos::Ptr<State>& S,
    Key wrt_key, const Teuchos::Ptr<CompositeVector>& result) {
  // Initialize the MeshPartition
  if (!models_->first->initialized()) {
    models_->first->Initialize(result->Mesh(), -1);
    models_->first->Verify();
  }

  Teuchos::RCP<const CompositeVector> pres = S->GetFieldData(pres_key_);
  Teuchos::RCP<const CompositeVector> poro = S->GetFieldData(poro_key_);
  const double& patm = *S->GetScalarData("atmospheric_pressure");

  if (wrt_key == pres_key_) {
    // evaluate the model
    for (CompositeVector::name_iterator comp=result->begin();
         comp!=result->end(); ++comp) {
      ASSERT(*comp == "cell");  // partition on cell only, could add boundary_face if needed (but not currently)
      const Epetra_MultiVector& pres_v = *(pres->ViewComponent(*comp,false));
      const Epetra_MultiVector& poro_v = *(poro->ViewComponent(*comp,false));
      Epetra_MultiVector& result_v = *(result->ViewComponent(*comp,false));

      int count = result->size(*comp);
      for (int id=0; id!=count; ++id) {
        result_v[0][id] = models_->second[(*models_->first)[id]]->DPorosityDPressure(poro_v[0][id], pres_v[0][id], patm);
      }
  }

  } else if (wrt_key == poro_key_) {
    // evaluate the model
    for (CompositeVector::name_iterator comp=result->begin();
         comp!=result->end(); ++comp) {
      ASSERT(*comp == "cell");  // partition on cell only, could add boundary_face if needed (but not currently)
      const Epetra_MultiVector& pres_v = *(pres->ViewComponent(*comp,false));
      const Epetra_MultiVector& poro_v = *(poro->ViewComponent(*comp,false));
      Epetra_MultiVector& result_v = *(result->ViewComponent(*comp,false));

      int count = result->size(*comp);
      for (int id=0; id!=count; ++id) {
        result_v[0][id] = models_->second[(*models_->first)[id]]->DPorosityDBasePorosity(poro_v[0][id], pres_v[0][id], patm);
      }
    }

  } else {
    ASSERT(0);
  }
}



} //namespace
} //namespace
} //namespace
