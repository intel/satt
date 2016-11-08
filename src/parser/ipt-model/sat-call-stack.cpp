/*
// Copyright (c) 2015 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "sat-call-stack.h"
#include "sat-log.h"
#include <cstdio>
#include <string>
#include <cinttypes>


namespace sat {

int call_stack::max_depth_ = 0;

void call_stack::check_for_max_depth()
{
    if (stack_ptr_->peak_ - stack_ptr_->low_water_mark_ > max_depth_) {
        max_depth_ = stack_ptr_->peak_ - stack_ptr_->low_water_mark_;
        SAT_LOG(1, "@ ! d %d\n", max_depth_);
    }
}

void call_stack::push(rva caller_nlip)
{
    stack_ptr_->stack_.push_back(caller_nlip);

    if (depth() > stack_ptr_->peak_) {
        stack_ptr_->peak_ = depth();
        check_for_max_depth();
    }
}

rva call_stack::pop(bool lost)
{
    rva pc = 0;
    if (stack_ptr_->stack_.empty()) {
        if (!lost) {
            --stack_ptr_->offset_;
            if (stack_ptr_->offset_ < stack_ptr_->low_water_mark_) {
                stack_ptr_->low_water_mark_ = stack_ptr_->offset_;
                check_for_max_depth();
            }
            SAT_LOG(1, "RET WITH AN EMPTY CALL STACK. ADJUST LOW-WATER MARK\n");
        }
    } else {
        pc = stack_ptr_->stack_.back();
        stack_ptr_->stack_.pop_back();
    }
    return pc;
}

void call_stack::iterate(std::function<void(rva)> callback) const
{
    for (auto r : stack_ptr_->stack_) {
        callback(r);
    }
}

}
