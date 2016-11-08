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
#ifndef SAT_SCHEDULING_HEURISTICS
#define SAT_SCHEDULING_HEURISTICS

#include <string>
#include <functional>

#include "sat-sideband-model.h"

namespace sat {


using namespace std;

class scheduling_heuristics_imp;

class scheduling_heuristics {
public:
    scheduling_heuristics(unsigned                         cpu,
                          const string&                    rtit_path,
                          shared_ptr<const sideband_model> sideband,
                          const vm_sec_list_type& vm_section_list);
   ~scheduling_heuristics();

    void apply();

    bool get_initial(rtit_pos& pos, uint64_t& tsc);
    bool get_current(rtit_offset current_offset,
                     uint64_t&   tsc,
                     uint64_t&   next_tsc,
                     tid_t&      tid,
                     rtit_pos&   next_schedule) const;
    bool get_tsc(rtit_pos  timing_packet_pos,
                 uint64_t& tsc,
                 uint64_t& next_tsc) const;
    bool get_next_valid_tsc(rtit_pos  current_pos,
                            rtit_pos& next_pos,
                            uint64_t& next_tsc);

    using callback_func = std::function<void(
                              pair<uint64_t, uint64_t> tsc,
                              tid_t                    tid,
                              pair<bool, bool>         has_pos,
                              pair<rtit_pos, rtit_pos> pos)>;
    void iterate_quantums(uint64_t first_tsc, callback_func callback) const;
    bool get_first_quantum_start(uint64_t& tsc, bool& has_pos, rtit_pos& pos);

    //bool get_tid_by_tsc(uint64_t tsc, tid_t& tid);
    //bool get_tid(rtit_offset offset, tid_t& tid) const;

    void dump(); // TODO: remove

private:
    shared_ptr<scheduling_heuristics_imp> imp_;
};

} // namespace sat

#endif
