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
#ifndef SAT_IPT_TSC_HEURISTICS_H
#define SAT_IPT_TSC_HEURISTICS_H

#include "sat-ipt.h"
#include "sat-memory.h"
#include "sat-ipt-parser-sideband-info.h"     // for tsc_ratio

namespace sat {

using namespace std;
const uint64_t MAX_TSC_VAL = 0;

typedef enum {
    TSC, MTC, TMA, OVF, PSB, PGE // TODO: BEGIN, SKIP, END?
} tsc_item_type;

class tsc_heuristics {
public:
    tsc_heuristics(const std::string& sideband_path);
    ~tsc_heuristics();

    bool parse_ipt(const std::string& path);
    void apply();
    bool get_tsc(ipt_pos pos, uint64_t& tsc, uint64_t& next_tsc);
    bool get_tsc_wide_range(ipt_pos pos, uint64_t& tsc, uint64_t& next_tsc);

    using callback_func = function<void(tsc_item_type            type,
                                        pair<ipt_pos, ipt_pos>   pos,
                                        bool                     has_tsc,
                                        pair<uint64_t, uint64_t> tsc)>;
    void iterate_tsc_blocks(callback_func callback) const;
    ipt_pos get_last_psb(ipt_pos current_pos);
    bool get_next_valid_tsc(ipt_pos  current_pos,
                            ipt_pos& next_pos,
                            uint64_t& next_tsc);

    void dump() const; // TODO: remove
    void dump_tscs() const;

private:
    class imp;
    unique_ptr<imp> imp_;
}; // tsc_heuristics

} // sat

#endif // SAT_IPT_TSC_HEURISTICS_H
