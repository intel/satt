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
#ifndef SAT_IPT_BLOCK
#define SAT_IPT_BLOCK

#include "sat-ipt.h"
#include "sat-tid.h"

namespace sat {

using namespace std;

class ipt_block {
public:
    enum {
        TRACE, BAD, SCHEDULE_IN, SCHEDULE_OUT
    }                        type_;
    pair<ipt_pos, ipt_pos>   pos_;
    pair<uint64_t, uint64_t> tsc_;
    bool                     has_tid_; // TODO: is this needed?
    tid_t                    tid_;
    unsigned                 cpu_;
    ipt_pos                  psb_; // last psb position before the block start
}; // ipt_block

} // namespace sat

#endif // SAT_IPT_BLOCK
