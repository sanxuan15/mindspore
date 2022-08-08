/**
 * Copyright 2021-2022 Huawei Technologies Co., Ltd
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

#include "ops/apply_adam_with_amsgrad.h"

#include <string>
#include <set>
#include <map>
#include <utility>

#include "ops/op_utils.h"
#include "abstract/ops/primitive_infer_map.h"
#include "utils/tensor_construct_utils.h"
#include "utils/check_convert_utils.h"
#include "mindapi/src/helper.h"

namespace mindspore {
namespace ops {
namespace {
abstract::ShapePtr ApplyAdamWithAmsgradInferShape(const PrimitivePtr &primitive,
                                                  const std::vector<AbstractBasePtr> &input_args) {
  for (const auto &item : input_args) {
    MS_EXCEPTION_IF_NULL(item);
  }
  auto prim_name = primitive->name();
  auto var_shape = input_args[0]->BuildShape();
  auto m_shape = input_args[1]->BuildShape();
  auto v_shape = input_args[2]->BuildShape();
  auto vhat_shape = input_args[3]->BuildShape();
  auto grad_shape = input_args[7]->BuildShape();
  auto beta1_power_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[4]->BuildShape())[kShape];
  auto beta2_power_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[5]->BuildShape())[kShape];
  auto lr_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[6]->BuildShape())[kShape];

  int64_t batch_rank = 0;
  if (primitive->HasAttr(kBatchRank)) {
    auto value_ptr = primitive->GetAttr(kBatchRank);
    batch_rank = GetValue<int64_t>(value_ptr);
  }

  (void)CheckAndConvertUtils::CheckInteger("beta1_power_shape size", beta1_power_shape.size(), kGreaterEqual,
                                           batch_rank, prim_name);
  (void)CheckAndConvertUtils::CheckInteger("beta2_power_shape size", beta2_power_shape.size(), kGreaterEqual,
                                           batch_rank, prim_name);
  (void)CheckAndConvertUtils::CheckInteger("lr_shape size", lr_shape.size(), kGreaterEqual, batch_rank, prim_name);

  if (var_shape->IsDynamic() || m_shape->IsDynamic() || v_shape->IsDynamic() || vhat_shape->IsDynamic() ||
      grad_shape->IsDynamic()) {
    return var_shape->cast<abstract::ShapePtr>();
  }

  // shape of var, m, v, vhat must be the same
  std::map<std::string, abstract::BaseShapePtr> same_shape_args_map;
  (void)same_shape_args_map.insert(std::make_pair("m", m_shape));
  (void)same_shape_args_map.insert(std::make_pair("v", v_shape));
  (void)same_shape_args_map.insert(std::make_pair("vhat", vhat_shape));
  (void)same_shape_args_map.insert(std::make_pair("grad", grad_shape));
  for (auto &elem : same_shape_args_map) {
    if (*elem.second != *var_shape) {
      MS_EXCEPTION(ValueError) << "For '" << prim_name << "', evaluator arg '" << elem.first
                               << "' and 'var' must have the same shape. But got '" << elem.first
                               << "' shape: " << elem.second->ToString() << ", 'var' shape: " << var_shape->ToString()
                               << ".";
    }
  }
  auto shape_ptr = var_shape->cast<abstract::ShapePtr>();
  return shape_ptr;
}

TypePtr ApplyAdamWithAmsgradInferType(const PrimitivePtr &prim, const std::vector<AbstractBasePtr> &input_args) {
  auto prim_name = prim->name();
  auto var_type = input_args[0]->BuildType();
  auto m_type = input_args[1]->BuildType();
  auto v_type = input_args[2]->BuildType();
  auto vhat_type = input_args[3]->BuildType();
  auto beta1_power_type = input_args[4]->BuildType();
  auto beta2_power_type = input_args[5]->BuildType();
  auto lr_type = input_args[6]->BuildType();
  auto grad_type = input_args[7]->BuildType();
  const std::set<TypePtr> valid_types = {kFloat16, kFloat32};
  // var, m, v, vhat, grad valid and must has the same type
  std::map<std::string, TypePtr> args;
  (void)args.insert(std::make_pair("var_type", var_type));
  (void)args.insert(std::make_pair("m_type", m_type));
  (void)args.insert(std::make_pair("v_type", v_type));
  (void)args.insert(std::make_pair("vhat_type", vhat_type));
  (void)args.insert(std::make_pair("grad_type", grad_type));
  (void)CheckAndConvertUtils::CheckTensorTypeSame(args, valid_types, prim_name);
  // beta1_power, beta2_power, lr type valid
  CheckAndConvertUtils::CheckTensorTypeValid("beta1_power_type", beta1_power_type, valid_types, prim_name);
  CheckAndConvertUtils::CheckTensorTypeValid("beta2_power_type", beta2_power_type, valid_types, prim_name);
  CheckAndConvertUtils::CheckTensorTypeValid("lr_type", lr_type, valid_types, prim_name);
  return var_type;
}
}  // namespace

void ApplyAdamWithAmsgrad::Init(const float beta1, const float beta2, const float epsilon, const bool use_locking) {
  this->set_beta1(beta1);
  this->set_beta2(beta2);
  this->set_epsilon(epsilon);
  this->set_use_locking(use_locking);
}

void ApplyAdamWithAmsgrad::set_beta1(const float beta1) { (void)this->AddAttr(kBeta1, api::MakeValue(beta1)); }

void ApplyAdamWithAmsgrad::set_beta2(const float beta2) { (void)this->AddAttr(kBeta2, api::MakeValue(beta2)); }

void ApplyAdamWithAmsgrad::set_epsilon(const float epsilon) { (void)this->AddAttr(kEpsilon, api::MakeValue(epsilon)); }

void ApplyAdamWithAmsgrad::set_use_locking(const bool use_locking) {
  (void)this->AddAttr(kUseLocking, api::MakeValue(use_locking));
}

float ApplyAdamWithAmsgrad::get_beta1() const {
  auto value_ptr = this->GetAttr(kBeta1);
  return GetValue<float>(value_ptr);
}

float ApplyAdamWithAmsgrad::get_beta2() const {
  auto value_ptr = this->GetAttr(kBeta2);
  return GetValue<float>(value_ptr);
}

float ApplyAdamWithAmsgrad::get_epsilon() const {
  auto value_ptr = this->GetAttr(kEpsilon);
  return GetValue<float>(value_ptr);
}

bool ApplyAdamWithAmsgrad::get_use_locking() const {
  auto value_ptr = this->GetAttr(kUseLocking);
  return GetValue<bool>(value_ptr);
}

MIND_API_OPERATOR_IMPL(ApplyAdamWithAmsgrad, BaseOperator);
AbstractBasePtr ApplyAdamWithAmsgradInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                          const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  const int64_t input_num = 8;
  (void)CheckAndConvertUtils::CheckInputArgs(input_args, kGreaterEqual, input_num, primitive->name());
  auto infer_type = ApplyAdamWithAmsgradInferType(primitive, input_args);
  auto infer_shape = ApplyAdamWithAmsgradInferShape(primitive, input_args);
  return abstract::MakeAbstract(infer_shape, infer_type);
}
REGISTER_PRIMITIVE_EVAL_IMPL(ApplyAdamWithAmsgrad, prim::kPrimApplyAdamWithAmsgrad, ApplyAdamWithAmsgradInfer, nullptr,
                             true);
}  // namespace ops
}  // namespace mindspore
