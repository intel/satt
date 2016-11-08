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
// TODO: - use initial tsc from sideband if needed
#include "sat-scheduling-heuristics.h"
#include "sat-rtit-tsc-heuristics.h"
#include "sat-file-input.h"
#include "sat-rtit-workarounds.h"
#include "sat-rtit-pkt-cnt.h"
#include "sat-log.h"
#include "sat-rtit-iterator.h"
#include <map>
#include <algorithm>
#include <cinttypes>

namespace sat {


struct scheduling {
    uint64_t    tsc;
    tid_t       prev;
    tid_t       tid;
    rtit_pos    pos; // absolute offset of the scheduling point
    rtit_offset distance; // distance to scheduling offset from sideband
    rtit_offset tip; // absolute offset of the scheduler TIP, or zero
};



const uint32_t PKT_CNT_MAX = 0x3FFF; // maximum RTIT_PKT_CNT value


class scheduling_heuristics_imp
{
public:
scheduling_heuristics_imp(unsigned                         cpu,
                          const string&                    rtit_path,
                          shared_ptr<const sideband_model> sideband,
                          const vm_sec_list_type& vm_section_list) :
    rtit_path_(rtit_path),
    initial_tid_from_sideband_(),
    pkt_mask_from_sideband_(),
    scheduler_tip_(),
    tsc_heuristics_(make_shared<tsc_heuristics>()
    )
{

    vm_section_list_ = vm_section_list;
    // 1st pass: apply tsc heuristics based on rtit
    tsc_heuristics_->parse_rtit(rtit_path_); // TODO: signal failure
    tsc_heuristics_->apply();
    uint64_t start, dummy;
    tsc_heuristics_->get_tsc({}, start, dummy); // TODO: signal failure

    // collect all schedulings from sideband
    sideband->iterate_schedulings(cpu,
                                  [this](uint64_t tsc, uint32_t pkt_cnt,
                                         tid_t prev, tid_t tid)
                                  {
                                      // insert new schedulings with
                                      // a distance that is larger than
                                      // any possible PKT_CNT
                                      schedulings_.insert({{tsc, pkt_cnt},
                                                           {tsc,
                                                            prev, tid, {},
                                                            PKT_CNT_MAX + 1,
                                                            0}
                                                          });
                                  });
    initial_tsc_from_sideband_ = sideband->initial_tsc();
    sideband->get_initial(cpu,
                          initial_tid_from_sideband_,
                          pkt_mask_from_sideband_);

    scheduler_tip_ = sideband->scheduler_tip();
}

void check_for_closest_scheduling_point(rtit_parser_output* output)
{
    // iterate inside the tsc slot
    rtit_offset pkt_cnt = rtit_parser_->policy.rtit_pkt_cnt();

    for (schedulings::iterator i = sched_begin_;
         i != sched_end_;
         ++i)
    {
        rtit_offset distance = i->first.second > pkt_cnt ?
            i->first.second - pkt_cnt :
            pkt_cnt - i->first.second;
        if (distance < i->second.distance) {
            i->second.pos      = output->parsed_.pos;
            i->second.distance = distance;
        }
    }
}

void emit(rtit_parser_output* output)
{
    rtit_parser_output::token_func f = output->parsed_.token;
    uint64_t a, b;
    if ((is_timing_packet(f) || output->parsed_.pos.offset_ == 0))
    {
#if 0
        for (auto i = sched_begin_; i != sched_end_; ++i) {
            printf("  %" PRIx64 "\n", i->second.pos.offset_);
        }
#endif

        //printf("TIMING PACKET %010" PRIx64 "\n", output->parsed_.pos.offset_);
        if (tsc_heuristics_->get_tsc(output->parsed_.pos, a, b)) {
            // start new tsc slot
            //printf("TSC SLOT %011" PRIx64 " .. %010" PRIx64 "\n", a, b);
            sched_begin_ = schedulings_.lower_bound({a, 0});
            sched_end_   = schedulings_.upper_bound({b, 0});
            if (sched_begin_ == sched_end_) {
                // no schedulings in this slot
                rtit_parser_->policy.skip_to_next_valid_timing_packet();
                //printf("  NO SCHEDULINGS\n");
            } else {
                unsigned count = 0;
                for (auto i = sched_begin_; i != sched_end_; ++i) {
                    ++count;
                }
                //printf("  %u SCHEDULING(S)\n", count);
                if (count) {
                    check_for_closest_scheduling_point(output);
                }
            }
        } else {
            // skip to next valid timing packet
            rtit_parser_->policy.skip_to_next_valid_timing_packet();
            //printf("  NO TSC\n");
        }
    } else {
        check_for_closest_scheduling_point(output);
    }
}

uint64_t curb_tsc(uint64_t tsc, bool is_pos, rtit_pos pos) const
{
    uint64_t result, a, b;

    if (is_pos &&
        (tsc_heuristics_->get_tsc(pos, a, b)) &&
        (tsc < a || tsc > b))
    {
        if (tsc < a) {
            result = a;
        } else { // tsc > b
            result = b;
        }
    } else {
        result = tsc;
    }

    return result;
}

void apply()
{
    // 2nd pass: apply scheduling heuristics based on rtit;
    // set up a parser for parsing through the rtit
    typedef file_input<rtit_parser_input> rtit_input;
    shared_ptr<rtit_input> input{new rtit_input};
    if (!input->open(rtit_path_)) {
        return; // TODO: signal failure
    }

    shared_ptr<rtit_parser_output> output{new rtit_parser_output};
    rtit_parser_ = make_shared<parser_type>(input, output);
    rtit_parser_->policy.route_emit_to(this);
    rtit_parser_->policy.set_tsc_heuristics(tsc_heuristics_);
    rtit_parser_->policy.skip_to_next_valid_timing_packet();
    rtit_parser_->policy.set_pkt_mask(pkt_mask_from_sideband_);
    sched_begin_ = sched_end_ = schedulings_.begin();

    rtit_parser_->parse();

#if 0
    printf("\nAFTER SECOND PASS (PKT_MASK %x)\n", pkt_mask_from_sideband_);
    dump();
#endif

enum VM_SCHED_STATE {
    VM_STATE_LINUX = 0,
    VM_STATE_TO_VM,
    VM_STATE_VM,
    VM_STATE_TO_LINUX,
    MAX_VM_STATES
};

//    const char* vm_states_str[MAX_VM_STATES] = {"VM_STATE_LINUX", "VM_STATE_TO_VM", "VM_STATE_VM", "VM_STATE_TO_LINUX"};

    // 3rd pass: collect scheduler TIPs from rtit
    // and match them with scheduling points
    VM_SCHED_STATE vm_state = VM_STATE_LINUX;
    auto next = schedulings_.begin();
    if (next != schedulings_.end()) {
        auto prev = next;
        ++next;
        tid_t to_tid = 0;
        tid_t from_tid = 0;
        rtit_iterator rtit(rtit_path_);
        rtit.iterate([&](const rtit_parser_output::item& parsed, const rtit_offset& pkt_cnt) {
            if (parsed.token == &rtit_parser_output::tip ||
                parsed.token == &rtit_parser_output::fup_far ||
                parsed.token == &rtit_parser_output::fup_pge ||
                parsed.token == &rtit_parser_output::fup_pgd ||
                parsed.token == &rtit_parser_output::fup_buffer_overflow ||
                parsed.token == &rtit_parser_output::fup_pcc)
                {
                // Check VM entry/exit point
                auto vm_section = vm_section_list_.upper_bound(parsed.fup.address);
                if (vm_section != vm_section_list_.begin())
                {
                    if (parsed.fup.address < vm_section->first) {
                        --vm_section;
                    }

                    if(parsed.fup.address < (vm_section->first + vm_section->second.size))
                    {
                        // execution is in vm section
                        if (vm_state == VM_STATE_LINUX) {
                            from_tid = prev->second.tid;
                            to_tid = vm_section->second.tid;
                            vm_state = VM_STATE_TO_VM;
                        }
                        else if (vm_state == VM_STATE_VM &&
                            vm_section->second.tid != to_tid)
                        {
                            from_tid = to_tid;
                            to_tid = vm_section->second.tid;
                            vm_state = VM_STATE_TO_VM;
                        }
                    } else {
                        // Execution is outside of vm sections
                        if (vm_state == VM_STATE_VM) {
                            from_tid = to_tid;
                            vm_state = VM_STATE_TO_LINUX;
                        }
                        to_tid = prev->second.tid;
                    }
                } else {
                    // Execution is outside of vm sections
                    if (vm_state == VM_STATE_VM) {
                        from_tid = to_tid;
                        vm_state = VM_STATE_TO_LINUX;
                    }
                    to_tid   = prev->second.tid;
                }
                if (vm_state == VM_STATE_TO_LINUX ||
                    vm_state == VM_STATE_TO_VM)
                {
                    uint64_t a, b;
                    if (tsc_heuristics_->get_tsc(parsed.pos, a, b))
                    {
                        vm_schedulings_[{a,pkt_cnt}] = {
                            a,
                            from_tid,
                            to_tid,
                            parsed.pos, // absolute offset of the scheduling point
                            0, // distance to scheduling offset from sideband
                            0  // absolute offset of the scheduler TIP, or zero

                        };
                    }
                    if (vm_state == VM_STATE_TO_LINUX) {
                        vm_state = VM_STATE_LINUX;
                    } else if (vm_state == VM_STATE_TO_VM) {
                        vm_state = VM_STATE_VM;
                    }
                }
            }
            if (parsed.token == &rtit_parser_output::tip)
            {
                /* Check Linux scheduling point */
                if (parsed.fup.address == scheduler_tip_)
                {
                    while (next != schedulings_.end() &&
                        next->second.pos <= parsed.pos)
                    {
                        prev = next;
                        ++next;
                    }
                    if (prev->second.pos < parsed.pos &&
                        (next == schedulings_.end() ||
                        next->second.pos > parsed.pos))
                    {
                        uint64_t a, b;
                        if ((tsc_heuristics_->get_tsc(parsed.pos, a, b) &&
                            a - 4096 <= prev->second.tsc &&
                            b > prev->second.tsc) ||
                            parsed.pos.offset_ - prev->second.pos.offset_ < 0x52)
                        {
                            if (prev->second.tip == 0) {
    #if 0
                                printf("ADJUST SCHEDULING POINT %"
                                    PRIx64 " -> %" PRIx64 " @ %" PRIx64 "\n",
                                    prev->second.pos.offset_, parsed.pos.offset_,
                                    prev->second.tsc);
    #endif
                                prev->second.tip = parsed.pos.offset_;
                                prev->second.pos = parsed.pos;
                            } else {
    #if 0
                                printf("DISCARD SECOND ADJUST %"
                                    PRIx64 " -> %" PRIx64 " @ %" PRIx64 "\n",
                                    prev->second.pos.offset_, parsed.pos.offset_,
                                    prev->second.tsc);
    #endif
                            }
                        } else {
    #if 0
                            printf("DISCARD OUT-OF-TSC-RANGE ADJUST %"
                                PRIx64 " -> %" PRIx64
                                " [%" PRIx64 "..%" PRIx64
                                "] %" PRIx64 " @ %" PRIx64 "\n",
                                prev->second.pos.offset_, parsed.pos.offset_,
                                a, b,
                                prev->second.tsc, prev->second.tsc);
    #endif
                        }
                    } else {
    #if 0
                        printf("DISCARD OUT-OF-RTIT-RANGE ADJUST %"
                            PRIx64 " -> %" PRIx64 " @ %" PRIx64 "\n",
                            prev->second.pos.offset_, parsed.pos.offset_,
                            prev->second.tsc);
    #endif
                    }
                }
            }
        });
    }

    // fourth pass: curb scheduling timestamps to their tsc windows
    for (auto sched = schedulings_.begin();
         sched != schedulings_.end();
         ++sched)
    {
        sched->second.tsc = curb_tsc(sched->second.tsc,
                                     sched->second.distance <= PKT_CNT_MAX,
                                     sched->second.pos);
    }

    /* Merge vm_schedulings into schedulings_ list */
     for (auto& vm_sched : vm_schedulings_)
     {
         schedulings_.insert(vm_sched);
     }
}

bool get_initial(rtit_pos& pos, uint64_t& tsc)
{
    return tsc_heuristics_->get_initial_tsc(pos, tsc);
}

bool get_current(rtit_offset current_offset,
                 uint64_t&   tsc,
                 uint64_t&   next_tsc,
                 tid_t&      tid,
                 rtit_pos&   next_schedule) const
{
    SAT_LOG(2, "get_current(%" PRIx64 ")\n", current_offset);
    bool got_it = false;

    if (get_tsc({current_offset}, tsc, next_tsc)) {
        //auto sched = schedulings_.lower_bound({tsc, 0});
        auto sched = find_if(schedulings_.begin(),
                             schedulings_.end(),
                             [current_offset](schedulings::const_reference s) {
                                 return s.second.pos.offset_ >= current_offset;
                             });
        if (sched != schedulings_.end())
        {
            tid = sched->second.prev;
            SAT_LOG(2, "GOT TID %u\n", tid);
            if (sched->second.tsc < next_tsc) {
                next_schedule = sched->second.pos;
            } else {
                next_schedule = {}; // use 0 to signal no scheduling in slot
                SAT_LOG(0, "NO SCHEDULINGS IN SLOT [%" PRIx64 "..%" PRIx64 ")\n",tsc, next_tsc);
            }
        } else if (sched != schedulings_.begin()) {
            --sched;
            tid           = sched->second.tid;
            next_schedule = {};
            SAT_LOG(0, "GOT TID %u FOR LAST SCHEDULE\n", tid);
        } else {
            tid           = initial_tid_from_sideband_;
            next_schedule = {};
            SAT_LOG(0, "USING INITIAL TID %u FROM SIDEBAND\n", tid);
        }
        got_it = true;
    } else {
        SAT_LOG(0, "COULD NOT GET TSC AT OFFSET %" PRIx64 "\n", current_offset);
    }

    return got_it;
}

bool get_tsc(rtit_pos timing_packet_pos, uint64_t& tsc, uint64_t& next_tsc) const
{
    return tsc_heuristics_->get_tsc(timing_packet_pos, tsc, next_tsc);
}

bool get_next_valid_tsc(rtit_pos  current_pos,
                        rtit_pos& next_pos,
                        uint64_t& next_tsc)
{
    return tsc_heuristics_->get_next_valid_tsc(current_pos, next_pos, next_tsc);
}

void iterate_quantums(uint64_t                             first_tsc,
                      scheduling_heuristics::callback_func callback) const
{
    uint64_t prev_tsc     = initial_tsc_from_sideband_;
    bool     prev_has_pos = true;
    rtit_pos prev_pos     = {};
    tid_t    prev_tid     = initial_tid_from_sideband_;
    bool     have_prev    = false;

    for (const auto& s : schedulings_) {
        uint64_t tsc     = s.second.tsc;
        bool     has_pos = s.second.distance <= PKT_CNT_MAX;
        rtit_pos pos     = s.second.pos;
        tid_t    tid     = s.second.tid;

        if (tsc > first_tsc) {
            if (!have_prev) {
                if (initial_tsc_from_sideband_ <= first_tsc) {
                    prev_tsc     = initial_tsc_from_sideband_;
                    prev_has_pos = false; // TODO: can we have offset?
                    prev_pos     = {};
                    prev_tid     = initial_tid_from_sideband_;
                } else {
                    // This should never happen, unless the trace is broken.
                    // (Yes, have seen a trace broken like this.)
                    // Just do something that keeps us going.
                    SAT_ERR("sideband INIT TSC is later than first RTIT TSC\n");
                    prev_tsc     = first_tsc;
                    prev_has_pos = false;
                    prev_pos     = {};
                    prev_tid     = initial_tid_from_sideband_;
                }
                have_prev = true;
            }
            callback({prev_tsc, tsc},
                     prev_tid,
                     {prev_has_pos, has_pos},
                     {prev_pos, pos});
        }

        prev_tsc     = tsc;
        prev_has_pos = has_pos;
        prev_pos     = pos;
        prev_tid     = tid;
        have_prev    = true;
    }

    if (have_prev) {
        callback({prev_tsc, numeric_limits<uint64_t>::max()},
                 prev_tid,
                 {prev_has_pos, false},
                 {prev_pos, {}});
    }
}

bool get_first_quantum_start(uint64_t& tsc, bool& has_pos, rtit_pos& pos)
{
    bool got_it = false;

    const auto s = schedulings_.begin();
    if (s != schedulings_.end()) {
        tsc     = s->second.tsc;
        has_pos = s->second.distance <= PKT_CNT_MAX;
        pos     = s->second.pos;
    }

    return got_it;
}

void dump()
{
    printf("SCHEDULER TIP: %" PRIx64 "\n", scheduler_tip_);
    printf("TIMESTAMP, PKT_CNT: (TIDS      )  =>  RTIT OFFSET (DISTANCE)\n"
           "------------------------------------------------------------\n");
    for (auto& i : schedulings_) {
        printf("%010" PRIx64 ", %05x:  (%3u -> %3u)  =>  %08" PRIx64 "  (%" PRIx64 ")\n",
               i.second.tsc,
               i.first.second,
               i.second.prev,
               i.second.tid,
               i.second.pos.offset_,
               i.second.distance);
    }
}

private:
    string                     rtit_path_;
    uint64_t                   initial_tsc_from_sideband_;
    tid_t                      initial_tid_from_sideband_;
    uint32_t                   pkt_mask_from_sideband_;
    rva                        scheduler_tip_;
    // TODO: it would be better to use vector for storing schedulings
    typedef map<pair<uint64_t /* tsc */, uint32_t /* pkt_cnt */>, scheduling>
    schedulings;
    schedulings                schedulings_;
    schedulings::iterator      sched_begin_;
    schedulings::iterator      sched_end_;
    shared_ptr<tsc_heuristics> tsc_heuristics_;
    typedef rtit_parser<
    postpone_early_mtc<
    synthesize_dropped_mtcs<
    pkt_cnt<
    skip_to_next_timing_packet_on_request<
    route_emit<scheduling_heuristics_imp>
    >>>>> parser_type;
    shared_ptr<parser_type> rtit_parser_;

    vm_sec_list_type vm_section_list_;
    schedulings vm_schedulings_;

}; // class scheduling_heuristics_imp


scheduling_heuristics::scheduling_heuristics(
                           unsigned                         cpu,
                           const string&                    rtit_path,
                           shared_ptr<const sideband_model> sideband,
                           const vm_sec_list_type& vm_section_list) :
    imp_{new scheduling_heuristics_imp(cpu, rtit_path, sideband, vm_section_list)}
{
}

scheduling_heuristics::~scheduling_heuristics()
{
}

void scheduling_heuristics::apply()
{
    imp_->apply();
}

void scheduling_heuristics::dump()
{
    imp_->dump();
}

bool scheduling_heuristics::get_initial(rtit_pos& pos, uint64_t& tsc)
{
    return imp_->get_initial(pos, tsc);
}

bool scheduling_heuristics::get_current(rtit_offset current_offset,
                                        uint64_t&   tsc,
                                        uint64_t&   next_tsc,
                                        tid_t&      tid,
                                        rtit_pos&   next_schedule) const
{
    return imp_->get_current(current_offset, tsc, next_tsc, tid, next_schedule);
}

bool scheduling_heuristics::get_tsc(rtit_pos  timing_packet_pos,
                                    uint64_t& tsc,
                                    uint64_t& next_tsc) const
{
    return imp_->get_tsc(timing_packet_pos, tsc, next_tsc);
}

bool scheduling_heuristics::get_next_valid_tsc(rtit_pos  current_pos,
                                               rtit_pos& next_pos,
                                               uint64_t& next_tsc)
{
    return imp_->get_next_valid_tsc(current_pos, next_pos, next_tsc);
}

void scheduling_heuristics::iterate_quantums(uint64_t      first_tsc,
                                             callback_func callback) const
{
    imp_->iterate_quantums(first_tsc, callback);
}

bool scheduling_heuristics::get_first_quantum_start(uint64_t& tsc,
                                                    bool&     has_pos,
                                                    rtit_pos& pos)
{
    return imp_->get_first_quantum_start(tsc, has_pos, pos);
}

} // namespace sat
