/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  Interface for a thermal conductivity model with two phases.

  License: BSD
  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#include "dbc.hh"
#include "thermal_conductivity_twophase_factory.hh"
#include "thermal_conductivity_twophase_evaluator.hh"

namespace Amanzi {
namespace Energy {
namespace EnergyRelations {

ThermalConductivityTwoPhaseEvaluator::ThermalConductivityTwoPhaseEvaluator(
      Teuchos::ParameterList& plist) :
    SecondaryVariableFieldEvaluator(plist) {
  my_key_ = plist_.get<std::string>("thermal conductivity key", "thermal_conductivity");
  setLinePrefix(my_key_+std::string(" evaluator"));

  poro_key_ = plist_.get<std::string>("porosity key", "porosity");
  dependencies_.insert(poro_key_);

  sat_key_ = plist_.get<std::string>("saturation key", "saturation_liquid");
  dependencies_.insert(sat_key_);

  ASSERT(plist_.isSublist("thermal conductivity parameters"));
  Teuchos::ParameterList sublist = plist_.sublist("thermal conductivity parameters");
  ThermalConductivityTwoPhaseFactory fac;
  tc_ = fac.createThermalConductivityModel(sublist);
}


ThermalConductivityTwoPhaseEvaluator::ThermalConductivityTwoPhaseEvaluator(
      const ThermalConductivityTwoPhaseEvaluator& other) :
    SecondaryVariableFieldEvaluator(other),
    poro_key_(other.poro_key_),
    sat_key_(other.sat_key_),
    tc_(other.tc_) {}

Teuchos::RCP<FieldEvaluator>
ThermalConductivityTwoPhaseEvaluator::Clone() const {
  return Teuchos::rcp(new ThermalConductivityTwoPhaseEvaluator(*this));
}


void ThermalConductivityTwoPhaseEvaluator::EvaluateField_(
      const Teuchos::Ptr<State>& S,
      const Teuchos::Ptr<CompositeVector>& result) {
  // pull out the dependencies
  Teuchos::RCP<const CompositeVector> poro = S->GetFieldData(poro_key_);
  Teuchos::RCP<const CompositeVector> sat = S->GetFieldData(sat_key_);

  for (CompositeVector::name_iterator comp=result->begin();
       comp!=result->end(); ++comp) {
    for (int i=0; i!=result->size(*comp); ++i) {
      (*result)(*comp, i) = tc_->ThermalConductivity((*poro)(*comp, i), (*sat)(*comp, i));
    }
  }
}


void ThermalConductivityTwoPhaseEvaluator::EvaluateFieldPartialDerivative_(
      const Teuchos::Ptr<State>& S, Key wrt_key,
      const Teuchos::Ptr<CompositeVector>& result) {
  ASSERT(0); // not implemented, not yet needed
}

} //namespace
} //namespace
} //namespace
