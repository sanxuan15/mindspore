# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

"""BatchToSpace op"""
from mindspore.ops.op_info_register import op_info_register, TBERegOp, DataType

batch_to_space_op_info = TBERegOp("BatchToSpace") \
    .fusion_type("OPAQUE") \
    .async_flag(False) \
    .binfile_name("batch_to_space_d.so") \
    .compute_cost(10) \
    .kernel_name("batch_to_space_d") \
    .partial_flag(True) \
    .attr("block_size", "required", "int", "all") \
    .attr("crops", "required", "listListInt", "all") \
    .input(0, "x", False, "required", "all") \
    .output(0, "y", False, "required", "all") \
    .dtype_format(DataType.F16_5HD, DataType.F16_5HD) \
    .get_op_info()


@op_info_register(batch_to_space_op_info)
def _batch_to_space_tbe():
    """BatchToSpace TBE register"""
    return
