/**
 * Copyright 2022 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "tools/converter/quantizer/quant_helper/propagete_quant_param_pass.h"
#include <unordered_map>
#include <memory>
#include <set>
#include <string>
#include <list>
#include <map>
#include <algorithm>
#include "src/common/log_adapter.h"
#include "tools/converter/quantizer/quantize_util.h"
#include "tools/optimizer/common/gllo_utils.h"
#include "tools/common/node_util.h"
#include "tools/lite_exporter/fetch_content.h"
#include "tools/optimizer/common/format_utils.h"
#include "ops/core_ops.h"

namespace mindspore::lite::quant {
namespace {
const std::set<PrimitivePtr> kSelfInferOperator = {
  prim::kPrimReshape,    prim::kPrimTranspose,     prim::kPrimSqueeze,      prim::kPrimUnsqueeze,
  prim::kPrimExpandDims, prim::kPrimMaxPoolFusion, prim::kPrimAvgPoolFusion};
}  // namespace
int PropagateQuantParamPass::PropagateSelf(const CNodePtr &cnode, bool forward) {
  if (CheckNodeInSet(cnode, kSelfInferOperator)) {
    auto curr_quant_holder = GetCNodeQuantHolder(cnode);
    CHECK_NULL_RETURN(curr_quant_holder);
    auto curr_input_quant_params = curr_quant_holder->get_input_quant_params();
    auto curr_output_quant_params = curr_quant_holder->get_output_quant_params();
    if (curr_input_quant_params.empty() || curr_output_quant_params.empty()) {
      return RET_OK;
    }
    std::string primitive_name;
    auto ret = opt::GetPrimitiveType(cnode, &primitive_name);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Get primitive type failed.";
      return ret;
    }
    MS_LOG(INFO) << primitive_name << ":" << cnode->fullname_with_scope() << " propagate self";
    auto curr_input_quant_param = curr_input_quant_params.at(0);
    auto curr_output_quant_param = curr_output_quant_params.at(0);
    if (forward) {
      // input->output
      if (IsValid(curr_input_quant_param) && !IsValid(curr_output_quant_param)) {
        curr_quant_holder->set_output_quant_param(0, curr_input_quant_param);
      }
    } else {
      // output->input
      if (!IsValid(curr_input_quant_param) && IsValid(curr_output_quant_param)) {
        curr_quant_holder->set_input_quant_param(0, curr_output_quant_param);
      }
    }
  }
  return RET_OK;
}

int PropagateQuantParamPass::ForwardTupleGetItem(const CNodePtr &cnode) {
  constexpr int tuple_get_item_input_size = 3;
  MS_CHECK_TRUE_MSG(cnode->size() == tuple_get_item_input_size, RET_ERROR, "cnode->size() != 3");
  auto index_node = cnode->input(THIRD_INPUT);
  auto index_value_node = index_node->cast<mindspore::ValueNodePtr>();
  if (index_value_node == nullptr) {
    MS_LOG(WARNING) << "index value node is null";
    return RET_NULL_PTR;
  }
  size_t index = static_cast<size_t>(opt::CastToInt(index_value_node->value()).front());
  auto input_node = cnode->input(SECOND_INPUT);
  MS_CHECK_TRUE_MSG(input_node != nullptr, RET_ERROR, "input_node == nullptr");
  auto input_cnode = input_node->cast<mindspore::CNodePtr>();
  MS_CHECK_TRUE_MSG(input_cnode != nullptr, RET_ERROR, "input_cnode == nullptr");
  auto input_cnode_primitive = GetValueNode<PrimitivePtr>(input_cnode->input(0));
  if (input_cnode_primitive == nullptr) {
    MS_LOG(WARNING) << "input_cnode_primitive is null";
    return RET_NULL_PTR;
  }
  auto input_primitive_quant_holder = GetCNodeQuantHolder(input_cnode_primitive);
  MS_CHECK_TRUE_MSG(input_primitive_quant_holder != nullptr, RET_NULL_PTR, "input_primitive_quant_holder is nullptr.");

  auto curr_quant_holder = GetCNodeQuantHolder(cnode);
  MS_CHECK_TRUE_MSG(curr_quant_holder != nullptr, RET_NULL_PTR, "curr_quant_holder is nullptr.");
  if (input_primitive_quant_holder->get_output_quant_params().size() > index) {
    auto quant_param = input_primitive_quant_holder->get_output_quant_params()[index];
    curr_quant_holder->set_input_quant_param(0, quant_param);
    curr_quant_holder->set_output_quant_param(0, quant_param);
  } else {
    MS_LOG(WARNING) << "this TupleGetItem node's input node: " << input_cnode->fullname_with_scope()
                    << "'s output quant_params size: " << input_primitive_quant_holder->get_output_quant_params().size()
                    << ", but index: " << index;
  }
  curr_quant_holder->set_quant_type(schema::QuantType_QUANT_ALL);
  return RET_OK;
}

int PropagateQuantParamPass::ForwardPropagate(const std::list<CNodePtr> &nodes) {
  for (const auto &cnode : nodes) {
    auto inputs = cnode->inputs();
    if (opt::CheckPrimitiveType(cnode, prim::kPrimTupleGetItem)) {
      if (ForwardTupleGetItem(cnode) != RET_OK) {
        MS_LOG(ERROR) << "Forward TupleGetItem " << cnode->fullname_with_scope() << " failed.";
        return RET_ERROR;
      }
      continue;
    }
    if (IsGraphInput(cnode) || opt::IsSpecialType(cnode) || opt::CheckPrimitiveType(cnode, prim::kPrimLstm)) {
      continue;
    }
    if (opt::CheckPrimitiveType(cnode, prim::kPrimLstm)) {
      continue;
    }
    // Infer quant param with forward (output->input).
    auto curr_quant_holder = GetCNodeQuantHolder(cnode);
    if (curr_quant_holder == nullptr) {
      continue;
    }
    auto curr_input_quant_params = curr_quant_holder->get_input_quant_params();
    for (size_t i = 0; i < curr_input_quant_params.size(); ++i) {
      auto quant_param = curr_input_quant_params.at(i);
      if (!quant_param.empty() && quant_param.at(0).inited) {
        continue;
      }
      auto index = i + kPrimOffset;
      if (!cnode->input(index)->isa<mindspore::CNode>()) {
        continue;
      }

      // Expand Nodes.
      auto origin_inputs = cnode->inputs();
      auto ret = RemoveIfDepend(cnode);
      if (ret != RET_OK) {
        MS_LOG(ERROR) << cnode->fullname_with_scope() << " RemoveIfDepend failed.";
        return ret;
      }
      ret = RemoveIfMakeTuple(cnode);
      if (ret != RET_OK) {
        MS_LOG(ERROR) << cnode->fullname_with_scope() << " RemoveIfMakeTuple failed.";
        return ret;
      }
      opt::RemoveIfMonad(cnode);
      auto before_cnode_map = opt::GetRealCertainVarInput(cnode, index);
      cnode->set_inputs(origin_inputs);

      auto before_cnode = before_cnode_map.first;
      size_t before_out_index = before_cnode_map.second;
      auto before_quant_holder = GetCNodeQuantHolder(before_cnode);
      CHECK_NULL_RETURN(before_quant_holder);
      auto before_output_quant_param = before_quant_holder->get_output_quant_params();
      if (before_output_quant_param.size() > before_out_index && before_quant_holder->IsOutputQuantParamsInited()) {
        MS_LOG(INFO) << before_cnode->fullname_with_scope() << " forward propagate to " << cnode->fullname_with_scope();
        curr_quant_holder->set_input_quant_param(i, before_output_quant_param.at(before_out_index));
      }
    }
    // Infer quant param with self.
    auto ret = PropagateSelf(cnode, true);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " propagate self failed.";
      return ret;
    }
  }
  return RET_OK;
}

int PropagateQuantParamPass::BackwardPropagate(const std::list<CNodePtr> &nodes) {
  auto ret = FindNodeDepends(nodes, &node_depends_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Find node depends failed.";
    return ret;
  }
  for (auto iter = nodes.rbegin(); iter != nodes.rend(); iter++) {
    auto cnode = *iter;
    auto inputs = cnode->inputs();
    if (IsGraphInput(cnode) || opt::IsSpecialType(cnode) || opt::CheckPrimitiveType(cnode, prim::kPrimLstm)) {
      continue;
    }
    if (opt::CheckPrimitiveType(cnode, prim::kPrimLstm)) {
      continue;
    }
    // Infer quant param with forward (output<-input).
    auto curr_quant_holder = GetCNodeQuantHolder(cnode);
    if (curr_quant_holder == nullptr) {
      continue;
    }
    auto curr_output_quant_params = curr_quant_holder->get_output_quant_params();
    for (size_t i = 0; i < curr_output_quant_params.size(); ++i) {
      if (IsValid(curr_output_quant_params.at(i))) {
        continue;
      }
      // output<-input
      auto depend_iter = node_depends_.find(cnode);
      if (depend_iter == node_depends_.end()) {
        MS_LOG(WARNING) << cnode->fullname_with_scope() << " find depend failed.";
        continue;
      }
      if (depend_iter->second.backwards.size() == 1) {
        auto input_cnode = depend_iter->second.backwards.at(0);
        if (BackwardPerNode(input_cnode, cnode, i) != RET_OK) {
          MS_LOG(WARNING) << cnode->fullname_with_scope() << "back propagate quant param at: " << i << " failed.";
          continue;
        }
      } else if (depend_iter->second.backwards.size() > 1) {
        if (cnode->isa<abstract::AbstractTuple>() && cnode->cast<abstract::AbstractTuplePtr>()->size() > 1) {
          // Single output, multiple references
          for (auto &input_cnode : depend_iter->second.backwards) {
            if (BackwardPerNode(input_cnode, cnode, i) != RET_OK) {
              MS_LOG(WARNING) << cnode->fullname_with_scope() << "back propagate quant param at: " << i << " failed.";
              continue;
            }
          }
        } else {
          MS_LOG(INFO) << "Support for multi output.";
        }
      }
    }
    // Infer quant param with self.
    ret = PropagateSelf(cnode, false);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " propagate self failed.";
      return ret;
    }
  }
  return RET_OK;
}

int PropagateQuantParamPass::FindNodeDepends(const std::list<CNodePtr> &nodes,
                                             std::map<CNodePtr, DependNodes> *node_depends) {
  for (const auto &cnode : nodes) {
    if (opt::IsSpecialType(cnode)) {
      continue;
    }
    // Expand Nodes.
    auto origin_inputs = cnode->inputs();
    auto ret = RemoveIfDepend(cnode);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " RemoveIfDepend failed.";
      return ret;
    }
    ret = RemoveIfMakeTuple(cnode);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " RemoveIfMakeTuple failed.";
      return ret;
    }
    opt::RemoveIfMonad(cnode);

    for (size_t i = 1; i < cnode->size(); i++) {
      // Associate the input node with the forward output
      if (!cnode->input(i)->isa<mindspore::CNode>()) {
        continue;
      }
      auto input_cnode = cnode->input(i)->cast<CNodePtr>();
      auto iter = node_depends->find(input_cnode);
      if (iter != node_depends->end()) {
        iter->second.backwards.push_back(cnode);
      }
    }
    node_depends->insert({cnode, {cnode->inputs(), {}}});
    cnode->set_inputs(origin_inputs);
  }
  return RET_OK;
}

int PropagateQuantParamPass::Propagate() {
  std::set<FuncGraphPtr> all_func_graphs{};
  quant::GetFuncGraphs(func_graph_, &all_func_graphs);
  // Support for multi-subgraph models
  for (auto &item : all_func_graphs) {
    auto status = DoSingleGraphPropagate(item);
    if (status != RET_OK) {
      MS_LOG(ERROR) << "Do Quantize failed.";
      return status;
    }
  }
  return RET_OK;
}

int PropagateQuantParamPass::DoSingleGraphPropagate(const FuncGraphPtr &fun_graph) {
  CHECK_NULL_RETURN(fun_graph);
  auto nodes = fun_graph->GetOrderedCnodes();

  for (const auto &cnode : nodes) {
    auto ret = PropagateSelf(cnode, true);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " propagate self forward failed";
      return ret;
    }
    ret = PropagateSelf(cnode, false);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << cnode->fullname_with_scope() << " propagate self backward failed";
      return ret;
    }
  }

  auto ret = ForwardPropagate(nodes);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Propagate forward failed.";
    return ret;
  }
  ret = BackwardPropagate(nodes);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Propagate backed failed.";
    return ret;
  }
  return RET_OK;
}

int PropagateQuantParamPass::BackwardPerNode(const CNodePtr &input_cnode, const CNodePtr &cnode, size_t curr_index) {
  MS_CHECK_TRUE_RET(input_cnode != nullptr, RET_ERROR);
  MS_CHECK_TRUE_RET(cnode != nullptr, RET_ERROR);
  auto curr_quant_holder = GetCNodeQuantHolder(cnode);
  MS_CHECK_TRUE_RET(curr_quant_holder != nullptr, RET_ERROR);
  auto curr_output_quant_param = curr_quant_holder->get_output_quant_params();
  MS_CHECK_FALSE_MSG(curr_output_quant_param.empty(), RET_ERROR, "Output quant params is empty.");
  if (IsValid(curr_output_quant_param.at(curr_index))) {
    return RET_OK;
  }
  // find input index
  size_t input_index = 0;
  auto iter = std::find(input_cnode->inputs().cbegin(), input_cnode->inputs().cend(), cnode);
  if (iter == input_cnode->inputs().cend()) {
    MS_LOG(ERROR) << cnode->fullname_with_scope() << "find backward node failed!";
    return RET_ERROR;
  } else {
    input_index = std::distance(input_cnode->inputs().cbegin(), iter) - kPrimOffset;
  }

  auto input_quant_holder = GetCNodeQuantHolder(input_cnode);
  MS_CHECK_TRUE_RET(input_quant_holder != nullptr, RET_ERROR);
  auto input_quant_params = input_quant_holder->get_input_quant_params();
  MS_CHECK_FALSE_MSG(input_quant_params.size() <= input_index, RET_ERROR,
                     input_cnode->fullname_with_scope() + " input quant params out_of_range.");
  if (IsValid(input_quant_params.at(input_index))) {
    MS_LOG(INFO) << input_cnode->fullname_with_scope() << " backward propagate to " << cnode->fullname_with_scope();
    curr_quant_holder->set_output_quant_param(curr_index, input_quant_params.at(input_index));
  }
  return RET_OK;
}

bool PropagateQuantParamPass::IsValid(const std::vector<schema::QuantParamT> quant_params) {
  return (!quant_params.empty() && quant_params.at(0).inited);
}
}  // namespace mindspore::lite::quant
