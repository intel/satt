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
#ifndef SAT_CALL_STACK_H
#define SAT_CALL_STACK_H

#include "sat-types.h"
#include <vector>
#include <functional>

namespace sat{

    using namespace std;
    typedef struct {
        vector<rva>  stack_;
        int          offset_;
        int          low_water_mark_;
        int          peak_;
    } stack_data;

    class call_stack {
    public:
        call_stack() : orig_stack_(), tmp_stack_()
        {
            temp_stack_disable();
        }

        void push(rva caller_nlip);
        rva pop(bool lost = false);
        void clear() { stack_ptr_->stack_.clear(); stack_ptr_->offset_ = 0; stack_ptr_->peak_ = 0; }
        void temp_stack_enable() { stack_ptr_ = &tmp_stack_; clear(); }
        void temp_stack_disable() { stack_ptr_ = &orig_stack_; }
        int depth() const { return stack_ptr_->stack_.size() + stack_ptr_->offset_; };
        int low_water_mark() const { return stack_ptr_->low_water_mark_; };

        void iterate(std::function<void(rva)> callback) const;

    private:
        void check_for_max_depth();

        stack_data   orig_stack_;
        stack_data   tmp_stack_;
        stack_data*  stack_ptr_;

        static int max_depth_;
    };

}

#endif // SAT_CALL_STACK_H
