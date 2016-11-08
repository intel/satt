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
#ifndef SAT_SIDEBAND_MODEL_H
#define SAT_SIDEBAND_MODEL_H

#include "sat-sideband-parser.h"
#include "sat-tid.h"
#include "sat-path-mapper.h"
#include "sat-types.h"
#include <string>
#include <memory>
#include <functional>

namespace sat {

    using namespace std;

    class sideband_model
    {
    public:
        void set_host_filesystem(shared_ptr<path_mapper> filesystem);
        bool build(const string& sideband_path);
        void iterate_schedulings(
                 unsigned cpu,
                 function<void(uint64_t /* tsc */,
                               uint64_t /* trace buffer offset */,
                               tid_t    /* prev tid */,
                               tid_t    /* tid */,
                               uint8_t  /* schedule_id */)> callback) const;
        void set_cr3(unsigned cr3, uint64_t tsc, tid_t tid);

        bool     get_initial(unsigned cpu, tid_t& tid, uint32_t& pkt_mask) const;
        uint64_t initial_tsc() const;
        string   process(pid_t pid) const;
        bool     get_thread_name(pid_t thread_id, string& name) const;
        bool     get_target_path(tid_t    tid,
                                 rva      address,
                                 uint64_t tsc,
                                 string&  path,
                                 rva&     start) const;
        bool     set_tid_for_target_path_resolution(tid_t tid);
        bool     get_target_path(rva      address,
                                 uint64_t tsc,
                                 string&  path,
                                 rva&     start) const;
        using callback_func = function<bool /*stop*/(const string& /*path*/,
                                                     rva           /*start*/)>;
        void     iterate_target_paths(tid_t         tid,
                                      uint64_t      tsc,
                                      callback_func callback);
        void adjust_for_hooks(rva& pc) const;
        rva scheduler_tip() const;
        bool get_schedule_id(uint64_t address, uint8_t& schedule_id) const;
        unsigned tsc_tick() const;
        unsigned fsb_mhz() const;
        uint32_t tsc_ctc_ratio() const;
        uint8_t mtc_freq() const;

    private:
#if 0
        shared_ptr<const executable> exe(unsigned           cr3,
                                         rva                address,
                                         unsigned long long tsc) const;
#endif
    };

}

#endif
