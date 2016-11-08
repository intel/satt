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

    class call_stack {
    public:
        call_stack() : stack_(), offset_(), low_water_mark_(), peak_() {}

        void push(rva caller_nlip);
        void pop(rva caller_nlip, bool lost = false);
        void clear() { stack_.clear(); offset_ = 0; peak_ = 0; }
        int depth() const { return stack_.size() + offset_; };
        int low_water_mark() const { return low_water_mark_; };

        void iterate(std::function<void(rva)> callback) const;

    private:
        void check_for_max_depth();

        vector<rva> stack_;
        int         offset_;
        int         low_water_mark_;
        int         peak_;

        static int max_depth_;
    };

}

#endif // SAT_CALL_STACK_H
