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
#include "sat-log.h"
#include "sat-ipt.h"
#include "sat-ipt-scheduling-heuristics.h"
#include "sat-ipt-tsc-heuristics.h"
#include "sat-ipt-iterator.h"
#include <map>

namespace sat {

string str_types[] = {"  -", "TIP", "OVF"};

struct scheduling_m {
    uint64_t         tsc;
    uint64_t         ipt_offset;
    tid_t            prev;
    tid_t            tid;
    bool             earmarked;
    size_t           offset;
    uint8_t          schedule_id;
    sched_sync_type  type;
}; // scheduling_m

const uint32_t PKT_CNT_MAX = 0x3FFF; // maximum IPT_PKT_CNT value

class scheduling_heuristics::imp
{
public:
    string         ipt_path_;
    using schedulings = map<uint64_t /*tsc*/, scheduling_m>;
    schedulings    schedulings_;
    rva            scheduler_tip_;
    shared_ptr<tsc_heuristics> tsc_heuristics_;
    shared_ptr<const sideband_model> sideband;
    tid_t          initial_tid_from_sideband_;
    uint32_t       pkt_mask_from_sideband_;
}; // scheduling_heuristics::imp


scheduling_heuristics::scheduling_heuristics(
                           unsigned                         cpu,
                           const string&                    ipt_path,
                           shared_ptr<const sideband_model> sideband,
                           const string&                    sideband_path) :
    imp_{new imp}
{
    imp_->ipt_path_ = ipt_path;
	imp_->tsc_heuristics_ = make_shared<tsc_heuristics>(sideband_path);
    imp_->tsc_heuristics_->parse_ipt(imp_->ipt_path_); // TODO: signal failure
    imp_->tsc_heuristics_->apply(); // TODO: signal failure

    // TODO: Check for OVERFLOWs for shceduligs has missing scheduler TIP

    imp_->sideband = sideband;

    // collect all schedulings from sideband
    sideband->iterate_schedulings(cpu,
                                  [this](uint64_t tsc, uint64_t ipt_offset,
                                         tid_t prev, tid_t tid, uint8_t schedule_id)
    {
        imp_->schedulings_.insert(
            {tsc,
                {tsc, ipt_offset, prev, tid, false, 0, schedule_id, sched_sync_type::TYPE_NONE}
            });
    });

    sideband->get_initial(cpu,
                          imp_->initial_tid_from_sideband_,
                          imp_->pkt_mask_from_sideband_);

    imp_->scheduler_tip_ = sideband->scheduler_tip();
}

scheduling_heuristics::~scheduling_heuristics()
{
}


void scheduling_heuristics::dump()
{
    printf("\n\nSCHEDULER TIP: %lx\n\n", imp_->scheduler_tip_);
    printf("TIMESTAMP :   (TIDS      ) =>  IPT OFFSET - SCH_ID  type (earm)\n");
    printf("-----------------------------------------------------------------------------\n");
    for (auto& sched : imp_->schedulings_)
    {
          printf("%010lx:   (%3d -> %3d) =>    %08lx -      %d   %s    (%d)\n",
            sched.second.tsc, sched.second.prev,
            sched.second.tid, sched.second.offset,
            sched.second.schedule_id, str_types[sched.second.type].c_str(),
            sched.second.earmarked);
    }
    printf("\n");
}

void scheduling_heuristics::apply()
{
    // TODO: schedule_id is useful for external trace destination (USB/PTI)
    //uint8_t schedule_id;

    // TODO: In scheduler_tip found case, do not start over from beginning
    //       of imp_->schedulings_ list but preserve previous end point and
    //       start next time from that.

    // IPT              offset
    //                   111          225                          488                   530                                    792    811   833
    //  ------------------|------------|-----|   O    |-------------|---------------------|--------|  O  |-| O || O |---| O |----|------|-----|-----
    //                   TIP          TIP       OVF                TIP                   TIP         OVF    OVF  OVF     OVF    TIP    TIP   TIP
    //                                /                            /                      |
    // SIDEBAND	                     |                             |                      |
    //              ipt_offset       /                            /                      /
    //      00          98          202        280               477                    519           599,599             704  782    796   826 
    // -----|-----------|------------|----------|-----------------|----------------------|-------------||------------------|----|------|-----|-oef
    //        

    ipt_iterator ipt(imp_->ipt_path_);
    ipt.iterate([&](const ipt_iterator::token& t, size_t offset) {
        if (t.func == &ipt_iterator::output::tip) {
            if (t.tip == imp_->scheduler_tip_) {
                // printf("#%08" PRIx64 ": SCHEDULER TIP %" PRIx64 " --> ",
                //        offset, t.tip);
                map<uint64_t /*tsc*/, scheduling_m>::iterator prev_iter = imp_->schedulings_.end();
                map<uint64_t /*tsc*/, scheduling_m>::iterator iter;
                //for (auto& sideband_scheduling : imp_->schedulings_) {
                bool found = false;
                for(iter = imp_->schedulings_.begin(); iter != imp_->schedulings_.end() && !found; iter++) {
                    //printf("%" PRIx64 " schedule item\n", iter->second.tsc);
                    if (iter->second.ipt_offset > offset) {
                        //printf("Found sched point that is greater than current TIP offset %" PRIx64 "\n", iter->second.ipt_offset);
                        if (prev_iter != imp_->schedulings_.end() &&
                            // TODO: schedule_id is useful for external trace destination (USB/PTI)
                            //prev_iter->second.schedule_id == schedule_id &&
                            !prev_iter->second.earmarked) {
                                // printf("%08" PRIx64 " \n", prev_iter->second.ipt_offset);
                                uint64_t a, b;
                                if (imp_->tsc_heuristics_->get_tsc(offset, a, b)) {
                                    prev_iter->second.tsc = a;
                                } else {
                                    printf("#COULD NOT GET TSC\n");
                                }
                                prev_iter->second.offset    = offset;
                                prev_iter->second.earmarked = true;
                                prev_iter->second.type      = sched_sync_type::TYPE_TIP;
                                found = true;
                                break;
                        } else {
                            printf("# Matching schedule point not found for first scheduler tip!!\n");
                            break;
                        }
                    } else {
                        //printf("# .....last sched point %" PRIx64 "\n", iter->second.ipt_offset);
                    }
                    prev_iter = iter;
                }
                // Fix the last schedule tip found from ipt stream
                //  search above tries to find sideband schedule having offset
                //  greater than the TIP offset and select the previous schedule.
                //  Because we are already the last schdule TIP, there are no
                //  schedule points found from sideband after that, so we need to
                //  just fit the last one to the current TIP.
                if (!found &&
                    prev_iter->second.ipt_offset < offset &&
                    !prev_iter->second.earmarked)
                {
                    prev_iter->second.offset    = offset;
                    prev_iter->second.earmarked = true;
                    prev_iter->second.type      = sched_sync_type::TYPE_TIP;
                }

            // TODO: schedule_id is useful for external trace destination (USB/PTI)
            // This can be slow, so if problems look here
            // } else if (imp_->sideband->get_schedule_id(t.tip, schedule_id)) {
            //     // DEBUG SCHEDULE_ID
            //     // printf("SCHEDULE_ID JUMP %d TIP %08" PRIx64 "\n", schedule_id, t.tip);
            } else {
                // DEBUG other tip
                //printf("tip %08" PRIx64 "\n", t.tip);
            }
        } else if (t.func == &ipt_iterator::output::ovf) {
            // In case of overflow, set all non-earmarked schedule points located in the
            // same tsc range with OVF packet to point to the OVF offset.
            struct scheduling_m *prev_item = nullptr;

            for (auto& sideband_scheduling : imp_->schedulings_) {
                //printf("%" PRIx64 "\n", sideband_scheduling.second.tsc);
                // Don't check earmarked in case we have accidentally linked
                //   wrong scheduler TIP to the sideband schedule point occurred in
                //   overflow phase, so we correct them here.
                if (sideband_scheduling.second.ipt_offset == offset &&
                    !sideband_scheduling.second.earmarked)
                {
                    sideband_scheduling.second.offset    = offset;
                    //sideband_scheduling.second.earmarked = true;
                    sideband_scheduling.second.type      = sched_sync_type::TYPE_OVF;
                    // no break here because possible many schedules within same overlow
                } else if (sideband_scheduling.second.ipt_offset > offset) {
                    if (prev_item != nullptr &&
                        prev_item->type == sched_sync_type::TYPE_NONE) {
                        prev_item->offset = offset;
                        prev_item->type = sched_sync_type::TYPE_OVF;
                        break;
                    } else {
                        //printf("# Matching schedule point not found for first scheduler tip!!\n");
                        break;
                    }
                }
                prev_item = &sideband_scheduling.second;
            }

        } else if (t.func == &ipt_iterator::output::eof) {
            //printf("#EOF!!!\n");
        }
    });
}

#if 0
void scheduling_heuristics::apply()
{
    //auto sideband_scheduling = imp_->schedulings_.begin();
    bool get_tnt = false;
    unsigned tnt_bit_count = 0;
    uint8_t schedule_id;

    ipt_iterator ipt(imp_->ipt_path_);
    ipt.iterate([&](const ipt_iterator::token& t, size_t offset) {
        if (t.func == &ipt_iterator::output::psb) {
            ;//printf("PSB\n");
        } else if (t.func == &ipt_iterator::output::tip) {
            if (t.tip == imp_->scheduler_tip_) {
                get_tnt = true;
                tnt_bit_count = 0;
                printf("#%08" PRIx64 ": SCHEDULER TIP %" PRIx64 " --> ",
                       offset, t.tip);
                uint64_t a, b;
                //if (imp_->tsc_heuristics_->get_tsc(offset, a, b)) {
                if (imp_->tsc_heuristics_->get_tsc_wide_range(offset, a, b)) {
                    printf("#%" PRIx64 "..%" PRIx64 "\n", a, b);

                    bool found = false;
                    for (auto& sideband_scheduling : imp_->schedulings_) {
                        //printf("%" PRIx64 "\n", sideband_scheduling.second.tsc);
                        if (sideband_scheduling.second.tsc >= a &&
                            sideband_scheduling.second.tsc <= b &&
                            sideband_scheduling.second.schedule_id == schedule_id &&
                            !sideband_scheduling.second.earmarked)
                        {
                            sideband_scheduling.second.offset    = offset;
                            sideband_scheduling.second.earmarked = true;
                            found                                = true;
                            break;
                        }
                    }
                    if (!found) {
                        //printf("#NOT FOUND\n");
                    } else {
                        //printf("#FOUND\n");
                    }
                } else {
                    printf("#COULD NOT GET TSC\n");
                }
                // This can be slow, so if problems look here
            } else if (imp_->sideband->get_schedule_id(t.tip, schedule_id)) {
                // DEBUG SCHEDULE_ID
                // printf("SCHEDULE_ID JUMP %d TIP %08" PRIx64 "\n", schedule_id, t.tip);
            } else {
                // DEBUG other tip
                //printf("tip %08" PRIx64 "\n", t.tip);
            }
        } else if (t.func == &ipt_iterator::output::ovf) {
            // In case of overflow, set all non-earmarked schedule points located in the
            // same tsc range with OVF packet to point to the OVF offset.
            uint64_t a, b;
            //if (imp_->tsc_heuristics_->get_tsc(offset, a, b)) {
            if (imp_->tsc_heuristics_->get_tsc_wide_range(offset, a, b)) {
                //printf("#%" PRIx64 "..%" PRIx64 "\n", a, b);

                for (auto& sideband_scheduling : imp_->schedulings_) {
                    //printf("%" PRIx64 "\n", sideband_scheduling.second.tsc);
                    if (sideband_scheduling.second.tsc >= a &&
                        sideband_scheduling.second.tsc <= b &&
                        sideband_scheduling.second.schedule_id == schedule_id &&
                        !sideband_scheduling.second.earmarked)
                    {
                        sideband_scheduling.second.offset    = offset;
                        // do not earmark sched point here in case the TIP would found later.
                    } else if (sideband_scheduling.second.tsc > b) {
                        break;
                    }
                }
            } else {
                printf("#COULD NOT GET TSC\n");
            }


        } else if (t.func == &ipt_iterator::output::eof) {
            //printf("#EOF!!!\n");
        }
    });
}
#endif

bool scheduling_heuristics::get_first_quantum_start(uint64_t& tsc, bool& has_offset, size_t& offset, tid_t& tid)
{
    bool got_it = false;

    for (const auto& s : imp_->schedulings_) {
        if (s.second.earmarked) {
            tsc        = s.second.tsc;
            has_offset = s.second.earmarked;
            offset     = s.second.offset;
            tid        = s.second.tid;
            got_it = true;
            break;
        }
    }

    return got_it;
}

void scheduling_heuristics::iterate_quantums(uint64_t     first_tsc,
                      scheduling_heuristics::callback_func callback) const
{
    uint64_t initial_tsc_from_sideband_ = imp_->sideband->initial_tsc();
    uint64_t prev_tsc     = initial_tsc_from_sideband_;
    bool     prev_has_pos = true;
    ipt_pos prev_pos     = {};
    tid_t    prev_tid     = imp_->initial_tid_from_sideband_;
    bool     have_prev    = false;

    for (const auto& s : imp_->schedulings_) {
        uint64_t tsc     = s.second.tsc;
        bool     has_pos = s.second.earmarked;
        ipt_pos  pos     = s.second.offset;
        tid_t    tid     = s.second.tid;

        if (tsc > first_tsc) {
            if (!have_prev) {
                if (initial_tsc_from_sideband_ <= first_tsc) {
                    prev_tsc     = initial_tsc_from_sideband_;
                    prev_has_pos = false; // TODO: can we have offset?
                    prev_pos     = {};
                    prev_tid     = imp_->initial_tid_from_sideband_;
                } else {
                    // This should never happen, unless the trace is broken.
                    // (Yes, have seen a trace broken like this.)
                    // Just do something that keeps us going.
                    SAT_ERR("sideband INIT TSC is later than first IPT TSC\n");
                    prev_tsc     = first_tsc;
                    prev_has_pos = false;
                    prev_pos     = {};
                    prev_tid     = imp_->initial_tid_from_sideband_;
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

} // sat
