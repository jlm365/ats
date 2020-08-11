/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/*
  Source term evaluator for enthalpy of mass source.

  Authors: Svetlana Tokareva (tokareva@lanl.gov)
*/

#ifndef AMANZI_LAKE_THERMO_RELATIONS_SOURCE_TERMS_EVALUATOR_
#define AMANZI_LAKE_THERMO_RELATIONS_SOURCE_TERMS_EVALUATOR_

#include "Factory.hh"
#include "secondary_variable_field_evaluator.hh"

namespace Amanzi {
namespace LakeThermo {

class LakeThermoSourceEvaluator : public SecondaryVariableFieldEvaluator {

 public:
  // constructor format for all derived classes
  explicit
  LakeThermoSourceEvaluator(Teuchos::ParameterList& plist);
  LakeThermoSourceEvaluator(const LakeThermoSourceEvaluator& other);

  virtual Teuchos::RCP<FieldEvaluator> Clone() const;

  // Required methods from SecondaryVariableFieldEvaluator
  virtual void EvaluateField_(const Teuchos::Ptr<State>& S,
          const Teuchos::Ptr<CompositeVector>& results);
  virtual void EvaluateFieldPartialDerivative_(const Teuchos::Ptr<State>& S,
          Key wrt_key, const Teuchos::Ptr<CompositeVector>& results);

 protected:
  void InitializeFromPlist_();

  Key temperature_key_;
  Key density_key_;
  Key internal_enthalpy_key_;
  Key external_enthalpy_key_;
  Key mass_source_key_;
  Key internal_density_key_;
  Key external_density_key_;
  Key conducted_source_key_;
  Key cell_vol_key_;

  bool include_conduction_;
  enum SourceUnits {
    SOURCE_UNITS_METERS_PER_SECOND,
    SOURCE_UNITS_MOLS_PER_SECOND,
    SOURCE_UNITS_MOLS_PER_SECOND_PER_METERSD
  };

  SourceUnits source_units_;
  
 private:
  static Utils::RegisteredFactory<FieldEvaluator,LakeThermoSourceEvaluator> factory_;

};

} //namespace
} //namespace

#endif