/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/*
  The column average temperature evaluator gets the subsurface temperature and number of cells (related to depth), and returns the average column temperature.

  Authors: Ahmad Jan (jana@ornl.gov)
*/

#include "activelayer_average_temp_evaluator.hh"

namespace Amanzi {
namespace Flow {



ActiveLayerAverageTempEvaluator::ActiveLayerAverageTempEvaluator(Teuchos::ParameterList& plist)
    : SecondaryVariableFieldEvaluator(plist)
{
  domain_ = Keys::getDomain(my_key_);
  auto pos = domain_.find_last_of('_');
  int col_id = std::stoi(domain_.substr(pos+1, domain_.size()));
  
  std::stringstream domain_ss;
  domain_ss << "column_"<< col_id;
  temp_key_ = Keys::getKey(domain_ss.str(),"temperature");
  dependencies_.insert(temp_key_);

  trans_width_ =  plist_.get<double>("transition width [K]", 0.2);
}
  

Teuchos::RCP<FieldEvaluator>
ActiveLayerAverageTempEvaluator::Clone() const
{
  return Teuchos::rcp(new ActiveLayerAverageTempEvaluator(*this));
}


void
ActiveLayerAverageTempEvaluator::EvaluateField_(const Teuchos::Ptr<State>& S,
        const Teuchos::Ptr<CompositeVector>& result)
{

  double trans_temp = 273.15 + 0.5*trans_width_;
  
  Epetra_MultiVector& res_c = *result->ViewComponent("cell",false);

  const auto& temp_c = *S->GetFieldData(temp_key_)->ViewComponent("cell", false);
  
  int col_cells = temp_c.MyLength();
  double temp_sum = 0;
  int count = 0 ;

  for (int i=0; i!=col_cells; ++i) {
      if (temp_c[0][i] >= trans_temp) {
        temp_sum += temp_c[0][i];
        count += 1;
      }
    }
  
  res_c[0][0] = count >0 ? temp_sum/count : 0.0;

}
  
void
ActiveLayerAverageTempEvaluator::EvaluateFieldPartialDerivative_(const Teuchos::Ptr<State>& S,
               Key wrt_key, const Teuchos::Ptr<CompositeVector>& result)
{}

 
// Custom EnsureCompatibility forces this to be updated once.
bool
ActiveLayerAverageTempEvaluator::HasFieldChanged(const Teuchos::Ptr<State>& S,
        Key request)
{
  bool changed = SecondaryVariableFieldEvaluator::HasFieldChanged(S,request);

  if (!updated_once_) {
    UpdateField_(S);
    updated_once_ = true;
    return true;
  }
  return changed;
}

void
ActiveLayerAverageTempEvaluator::EnsureCompatibility(const Teuchos::Ptr<State>& S)
{

  AMANZI_ASSERT(my_key_ != std::string(""));
   
  Teuchos::RCP<CompositeVectorSpace> my_fac = S->RequireField(my_key_, my_key_);
  
  // check plist for vis or checkpointing control
  bool io_my_key = plist_.get<bool>(std::string("visualize ")+my_key_, true);
  S->GetField(my_key_, my_key_)->set_io_vis(io_my_key);
  bool checkpoint_my_key = plist_.get<bool>(std::string("checkpoint ")+my_key_, false);
  S->GetField(my_key_, my_key_)->set_io_checkpoint(checkpoint_my_key);
  
  if (my_fac->Mesh() != Teuchos::null) {
    // Recurse into the tree to propagate info to leaves.
    for (KeySet::const_iterator key=dependencies_.begin();
         key!=dependencies_.end(); ++key) {
      S->RequireFieldEvaluator(*key)->EnsureCompatibility(S);
    }
  }
}


} //namespace
} //namespace
