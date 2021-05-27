# Copyright 2021 Huawei Technologies Co., Ltd
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

"""Mul op"""
from mindspore.ops.op_info_register import op_info_register, CpuRegOp, DataType

mul_op_info = CpuRegOp("Mul") \
    .input(0, "x", "required") \
    .input(1, "y", "required") \
    .output(0, "output", "required") \
    .dtype_format(DataType.F32_Default, DataType.F32_Default, DataType.F32_Default) \
    .dtype_format(DataType.I32_Default, DataType.I32_Default, DataType.I32_Default) \
    .get_op_info()


@op_info_register(mul_op_info)
def _mul_cpu():
    """Mul cpu register"""
    return
