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
#ifndef SAT_IPT_SCHEDULING_HEURISTICS
#define SAT_IPT_SCHEDULING_HEURISTICS

#include "sat-ipt.h"
#include "sat-memory.h"
#include "sat-sideband-model.h"

namespace sat {

using namespace std;

typedef enum {
    TYPE_NONE, TYPE_TIP, TYPE_OVF
} sched_sync_type;

class scheduling_heuristics {
public:
    scheduling_heuristics(unsigned                         cpu,
                          const string&                    ipt_path,
                          shared_ptr<const sideband_model> sideband,
                          const string&                    sideband_path);
   ~scheduling_heuristics();

    bool get_first_quantum_start(uint64_t& tsc, bool& has_pos, ipt_pos& pos, tid_t& tid);
    void apply();
    using callback_func = std::function<void(
                              pair<uint64_t, uint64_t> tsc,
                              tid_t                    tid,
                              pair<bool, bool>         has_pos,
                              pair<ipt_pos, ipt_pos> pos)>;
    void iterate_quantums(uint64_t first_tsc, callback_func callback) const;
    void dump();

private:
    class           imp;
    unique_ptr<imp> imp_;
}; // scheduling_heuristics

} // sat

#endif // SAT_IPT_SCHEDULING_HEURISTICS
