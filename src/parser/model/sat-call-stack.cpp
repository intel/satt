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
    if (peak_ - low_water_mark_ > max_depth_) {
        max_depth_ = peak_ - low_water_mark_;
        printf("@ ! d %d\n", max_depth_);
    }
}

void call_stack::push(rva caller_nlip)
{
    stack_.push_back(caller_nlip);

    if (depth() > peak_) {
        peak_ = depth();
        check_for_max_depth();
    }
}

void call_stack::pop(rva caller_nlip, bool lost)
{
    if (stack_.empty()) {
        if (!lost) {
            --offset_;
            if (offset_ < low_water_mark_) {
                low_water_mark_ = offset_;
                check_for_max_depth();
            }
            SAT_LOG(1, "RET WITH AN EMPTY CALL STACK. ADJUST LOW-WATER MARK\n");
        }
    } else {
        // pop enough stack to drop the address
        string function;
        //get_location(caller_nlip, function);
        unsigned depth  = 0;
        unsigned to_pop = 0;
        auto p = stack_.rbegin();
        for (; p != stack_.rend(); ++p) {
            if (*p == caller_nlip) {
                if (depth != 0) {
                    SAT_LOG(1, "FOUND TIP (%s) %u DEEP "
                           "(of %" PRIu64 ") WE MUST HAVE BEEN LOST.\n",
                           function.c_str(),
                           depth,
                           (uint64_t)stack_.size());
                }
                to_pop = depth + 1;
            }
            ++depth;
        }
        if (to_pop) {
            if (to_pop > 1) {
                SAT_LOG(1, "popping %u\n", to_pop);
            }
            do {
                //SAT_LOG(2, "POPPING %s\n", get_location(stack_.back()).c_str());
                stack_.pop_back();
            } while (--to_pop);
        } else {
            SAT_LOG(1, "HUH? NOT POPPING ANY (of %" PRIu64 "). WERE WE LOST?\n",
                   (uint64_t)stack_.size());
        }
        //print_location(caller_nlip);
        // success
    }
}

void call_stack::iterate(std::function<void(rva)> callback) const
{
    for (auto r : stack_) {
        callback(r);
    }
}

}
