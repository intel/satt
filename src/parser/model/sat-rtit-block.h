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
#ifndef SAT_RTIT_BLOCK
#define SAT_RTIT_BLOCK

#include "sat-rtit-parser.h"
#include "sat-tid.h"

namespace sat {

using namespace std;

class rtit_block {
public:
    enum {
        RTIT, BAD, SCHEDULE_IN, SCHEDULE_OUT
    }                        type_;
    pair<rtit_pos, rtit_pos> pos_;
    pair<uint64_t, uint64_t> tsc_;
    bool                     has_tid_; // TODO: is this needed?
    tid_t                    tid_;
    unsigned                 cpu_;
}; // rtit_block

} // namespace sat

#endif // SAT_RTIT_BLOCK
