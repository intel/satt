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
#include "sat-ipt-tsc-heuristics.h"
#include "sat-ipt-parser.h"
#include "sat-input.h"
#include "sat-ipt-iterator.h"
#include <map>
#include <set>
#include <vector>
#include <algorithm>


namespace sat {

struct tsc_item {
/*    enum {
        TSC, MTC, TMA, OVF, PSB, PGE // TODO: BEGIN, SKIP, END?
    } type;*/
    tsc_item_type type;
    uint8_t  mtc;
    uint64_t tsc;
    uint16_t ctc;  // for TMA
    uint16_t fast; // for TMA
    uint     pass;
    uint64_t tsc_in_next_mtc;
    uint     mtc_count; // for TMA
    bool     known_rollover_points; // for TMA
}; // tsc_item


using tscs = map<ipt_pos, tsc_item>;
using strts = map<ipt_pos, tsc_item_type>;

using tsc_value = tscs::value_type;

using tsc_range = pair<tscs::iterator, tscs::iterator>;

// an IPT block delimited by two MTCs that have tsc values
// (unless the MTCs happen to be at the beginning or end of the trace)
struct tsc_block {
    tsc_range         mtc;
    vector<tsc_range> gaps;
};

struct tsc_collection {
    tsc_collection() : has_head(), head(), blocks(), has_tail(), tail() {}

    bool              has_head;
    tsc_block         head;
    vector<tsc_block> blocks;
    bool              has_tail;
    tsc_block         tail;
};

namespace {

// TODO: remove
void dump_tsc(const tsc_value& t)
{
    switch (t.second.type) {
        case tsc_item_type::TSC:
            printf("#%08" PRIx64 ": tsc       %010" PRIx64 "\n",
                   t.first, t.second.tsc);
            break;
        case tsc_item_type::MTC:
            printf("#%08" PRIx64 ": mtc %02x -> %010" PRIx64,
                   t.first, t.second.mtc, t.second.tsc);
            if (t.second.pass) {
                printf("# pass %d\n", t.second.pass);
            } else {
                printf("\n");
            }
            break;
        case tsc_item_type::TMA:
            printf("#%08" PRIx64 ": tma %04x, %03x - mtc:%02x mtc_count:%04u tsc:%010lx  -  pass:%u\n",
                   t.first, t.second.ctc, t.second.fast, t.second.mtc, t.second.mtc_count, t.second.tsc, t.second.pass);
            break;
        case tsc_item_type::OVF:
            printf("#%08" PRIx64 ": ovf (%010" PRIx64 ")\n", t.first, t.second.tsc);
            break;
        case tsc_item_type::PSB:
            printf("#%08" PRIx64 ": psb (%010" PRIx64 ")\n", t.first, t.second.tsc);
            break;
        case tsc_item_type::PGE:
            printf("#%08" PRIx64 ": pge\n", t.first);
            break;
        default:
            printf("#%08" PRIx64 ": UNKNOWN\n", t.first);
            break;
    }
}

// TODO: remove
void dump_block(const tsc_block& block)
{
    dump_tsc(*block.mtc.first);
    printf("#...\n");
    dump_tsc(*block.mtc.second);
}

void build_tsc_collection(tscs& tscs, tsc_collection& collection)
{
    auto a        = tscs.begin();
    auto last_mtc = tscs.end();

    // skip until the first MTC
    while (a != tscs.end() && a->second.type != tsc_item_type::MTC) {
        ++a;
    }

    // loop through tscs, collecting ranges between MTCs that have tscs
    while (a != tscs.end()) {
        // a is now at the beginning of the range; move b to the end
        last_mtc = a;
        auto b = a;
        ++b;
        while (b != tscs.end()) {
            if (b->second.type == tsc_item_type::MTC) {
                last_mtc = b;
                if (b->second.pass) {
                    break;
                }
            }
            ++b;
        }
        if (b != tscs.end()) {
            // found a range from a to b; add it to the collection
            if (!a->second.pass) {
                collection.head     = {{a, b}};
                collection.has_head = true;
            } else {
                collection.blocks.push_back({{a, b}});
            }
        } else {
            // hit the end of the trace
            if (a != last_mtc) {
                // there was a partial range at the end of the trace; add it
                collection.tail     = {{a, last_mtc}};
                collection.has_tail = true;
            }
            break; // DONE
        }

        a = b;
    }
}

int adjacent_mtc_delta(int from, int to)
{
    if (from >= to) {
        to += 256;
    }
    return to - from;
}

void detect_mtc_gaps(tsc_block& block)
{
    auto           curr = block.mtc.first;
    tscs::iterator prev_mtc;
    bool           have_prev;

    if (curr->second.type == tsc_item_type::MTC) {
        prev_mtc  = curr;
        have_prev = true;
    } else {
        have_prev = false;
    }

    do {
        ++curr;
        if (curr->second.type == tsc_item_type::MTC) {
            if (have_prev &&
                adjacent_mtc_delta(prev_mtc->second.mtc, curr->second.mtc) > 1)
            {
                block.gaps.push_back({prev_mtc, curr});
            }
            prev_mtc  = curr;
            have_prev = true;
        }
    } while (curr != block.mtc.second);
}

void detect_mtc_gaps(vector<tsc_block>& blocks)
{
    for (auto& block : blocks) {
        detect_mtc_gaps(block);
    }
}

void detect_mtc_gaps(tsc_collection& collection)
{
    if (collection.has_head) {
        detect_mtc_gaps(collection.head);
    }
    detect_mtc_gaps(collection.blocks);
    if (collection.has_tail) {
        detect_mtc_gaps(collection.tail);
    }
}

// minimum number of missing MTCs in a block, if only looking at MTC gaps
int total_minimum_mtc_gaps(const tsc_block& block)
{
    int total = 0;
    for (const auto& gap : block.gaps) {
        total += adjacent_mtc_delta(gap.first->second.mtc,
                                    gap.second->second.mtc) - 1;
    }
    return total;
}

// number of MTCs in a block
int total_mtcs(const tsc_block& block)
{
    int total = 0;

    for (auto t = block.mtc.first; t != block.mtc.second; ++t) {
        if (t->second.type == tsc_item_type::MTC) {
            ++total;
        }
    }

    return total;
}

// estimated maximum number of MTCs in a block, assuming the tsc/mtc ratio
int estimated_max_mtcs(const tsc_block& block, uint64_t tsc_mtc_ratio)
{
    return ((block.mtc.second->second.tsc - block.mtc.first->second.tsc) +
            (tsc_mtc_ratio / 2)) /
           tsc_mtc_ratio;
}

#if 0
uint64_t median_tsc_mtc_ratio(const tsc_collection& collection)
{
    uint           ratio = 0;
    using ratio_t = pair<double, pair<uint64_t, uint64_t>>;
    vector<ratio_t> tsc_mtc_ratios;

    // loop through gapless MTC blocks, collecting ratios
    for (const auto& block : collection.blocks) {
        if (block.gaps.size() == 0) {
            uint64_t mtc_delta = total_mtcs(block);
            uint64_t tsc_delta = block.mtc.second->second.tsc -
                                 block.mtc.first->second.tsc;
            tsc_mtc_ratios.push_back({(double)tsc_delta / (double)mtc_delta,
                                     {tsc_delta, mtc_delta}});
        }
    }

    auto ratios = tsc_mtc_ratios.size();
    if (ratios > 0) {
        nth_element(tsc_mtc_ratios.begin(),
                    tsc_mtc_ratios.begin() + ratios / 2,
                    tsc_mtc_ratios.end());
        ratio = tsc_mtc_ratios[ratios / 2].first;
        printf("ratio = %lf -> %x (%u) = %lx / %lx (%lu / %lu)\n",
               tsc_mtc_ratios[ratios / 2].first,
               ratio, ratio,
               tsc_mtc_ratios[ratios / 2].second.first,
               tsc_mtc_ratios[ratios / 2].second.second,
               tsc_mtc_ratios[ratios / 2].second.first,
               tsc_mtc_ratios[ratios / 2].second.second);
    }


    return ratio;
}
#endif
#if 0
uint64_t median_tsc_mtc_ratio(const tsc_collection& collection)
{
    uint           ratio = 0;
    vector<double> tsc_mtc_ratios;

    // loop through gapless MTC blocks, collecting ratios
    for (const auto& block : collection.blocks) {
        if (block.gaps.size() == 0) {
            uint64_t mtc_delta = total_mtcs(block);
            uint64_t tsc_delta = block.mtc.second->second.tsc -
                                 block.mtc.first->second.tsc;
            tsc_mtc_ratios.push_back((double)tsc_delta / (double)mtc_delta);
        }
    }

    auto ratios = tsc_mtc_ratios.size();
    if (ratios > 0) {
        nth_element(tsc_mtc_ratios.begin(),
                    tsc_mtc_ratios.begin() + ratios / 2,
                    tsc_mtc_ratios.end());
        ratio = tsc_mtc_ratios[ratios / 2];
    }

    return ratio;
}
#endif

bool get_mtc_delta(const     tsc_block& block,
                   uint64_t  tsc_mtc_ratio,
                   uint64_t& mtc_delta)
{
    uint64_t mtcs = total_mtcs(block);
    uint64_t gaps = total_minimum_mtc_gaps(block);
    mtc_delta = mtcs + gaps;

    return (uint64_t)estimated_max_mtcs(block, tsc_mtc_ratio) < mtc_delta + 256;
}


inline uint64_t tscs_for_one_mtc(uint32_t tscs_to_ctc, uint8_t mtc_shift)
{
    return (1<<mtc_shift)*tscs_to_ctc;
}

inline uint64_t tscs_for_mtc_rollover(uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    return 255 * tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
}


/*
1. Find complete TMA-to-TMA blocks, update first TMA object:
  - Add MTC count between this and next TSC (less or greater than 255)
  - calculate artificial MTC value for TMA (CTC >> 9), without MSB bit
  - Real mtc count

  (- Is MTC count rollover)
  (- Min/Max MTC value (?))

  - Is_complete_tsc_block?  (TMA to TMA) => pass = 1 or 0
    - exact amount MTC's present
    - first TMA's mtc value + 1 == first MTC's mtc value
    - second TMA's mtc value == last MTC's mtc value
    example:
    calculted MTC count = 3
     TMA "5"
     MTC  6
     MTC  7
     MTC  8
     TMA "8"
  => OUTPUT: Mark complete TMA-to-TMA blocks (all MTC's arrived and mtc values matches with TMA->mtc values)
*/
void tsc_heuristics_first_round(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_shift)
{
    tsc_item* first_tma_obj   = nullptr;
    tsc_item* second_tma_obj   = nullptr;
    uint64_t  tsc         = 0;
    bool      have_tsc    = false;
    unsigned  mtc_count   = 0;
    bool      mtc_gaps    = false;

    tsc_item* last_mtc   = nullptr;

    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc = t.second.tsc;
            have_tsc = true;
            break;
        case tsc_item_type::TMA:
            if (have_tsc) {
                // copy tsc value from TSC packet.
                t.second.tsc = tsc;
                // Calculate in which mtc range the TSC/TMA pair arrives.
                t.second.mtc = (t.second.ctc >> mtc_shift) & 0xff;
                uint32_t next_ctc_from_tma = (t.second.ctc + 1)&0xffff;
                uint32_t ctc_ticks_to_next_mtc_packet = (1 << mtc_shift) - (next_ctc_from_tma&0x1ff);
                t.second.tsc_in_next_mtc = t.second.tsc + t.second.fast + (ctc_ticks_to_next_mtc_packet * tscs_to_ctc);
                second_tma_obj = &t.second;
                if(last_mtc) {
                    if (last_mtc->mtc != t.second.mtc) {
                        mtc_gaps = true;
                    }
                }

                // When reaching second TMA, we enter this if block:
                if (first_tma_obj) {
                    first_tma_obj->mtc_count = ((second_tma_obj->tsc - first_tma_obj->tsc) / tscs_for_one_mtc(tscs_to_ctc, mtc_shift));
                    if ( !mtc_gaps && mtc_count && first_tma_obj->mtc_count + 1  >= mtc_count)
                    {
                        first_tma_obj->pass = 1;
                        //printf("%08lx: TMA -  pass 1, arrived MTC count: 0x%x, calc mtc count: 0x%x\n", t.first, mtc_count, first_tma_obj->mtc_count);
                    }
                }
            }
            // Going to next TSC-to-TSC block, clear pointers.
            first_tma_obj = second_tma_obj;
            mtc_count = 0;
            last_mtc = nullptr;
            mtc_gaps = false;
            break;
        case tsc_item_type::MTC:
            if (!last_mtc) {
                if (first_tma_obj && (first_tma_obj->mtc + 1 != t.second.mtc)) {
                    mtc_gaps = true;
                }
            } else {
                if (last_mtc->mtc + 1 != t.second.mtc) {
                    mtc_gaps = true;
                }
            }
            last_mtc = &t.second;
            mtc_count++;
            break;
        default:
            break;
        }
    }
}


void adjust_first_tma_mtc_value(tsc_item* first_tma_obj, tsc_item* second_tma_obj)
{
    uint8_t calculated_first_tma_mtc = (second_tma_obj->mtc - first_tma_obj->mtc_count) & 0xff;
    uint8_t mtc_diff = (calculated_first_tma_mtc > first_tma_obj->mtc) ?
                     (calculated_first_tma_mtc - first_tma_obj->mtc) :
                     (first_tma_obj->mtc - calculated_first_tma_mtc);

    // Big safety marginal due to possible drifting
    if (mtc_diff > 10) {
        // MSB is missing, add it:
        //printf("fix MTC MSB: mtc(%x->%x) ,tsc(%lx), \n",
        //        first_tma_obj->mtc, first_tma_obj->mtc | 0x80, first_tma_obj->tsc);
        first_tma_obj->mtc |= 0x80;
    }
    first_tma_obj->pass = 2;
    // else: MSB bit is correct in ctc generated mtc value in first_tma_obj, no need to touch it.
}

/* helperr function for tsc_heuristics_second_round */
void adjust_second_tma_mtc_value(tsc_item* first_tma_obj, tsc_item* second_tma_obj)
{
    uint8_t calculated_second_tma_mtc = (first_tma_obj->mtc + first_tma_obj->mtc_count) & 0xff;
    uint8_t mtc_diff = (calculated_second_tma_mtc > second_tma_obj->mtc) ?
                     (calculated_second_tma_mtc - second_tma_obj->mtc) :
                     (second_tma_obj->mtc - calculated_second_tma_mtc);

    // Big safety marginal due to possible drifting
    if (mtc_diff > 10) {
        // MSB is missing, add it:
        //printf("fix MTC MSB: mtc(%x->%x) ,tsc(%lx), \n",
        //        second_tma_obj->mtc, second_tma_obj->mtc | 0x80, second_tma_obj->tsc);
        second_tma_obj->mtc |= 0x80;
    }
    second_tma_obj->pass = 2;
    // else: MSB bit is correct in ctc generated mtc value in first_tma_obj, no need to touch it.
}

/*
2. Valid mtc value to TMAs
  - Iterate to first complete TMA (pass == 1)
  - fill TMA->mtc backward (if needed) with reverse iterator to the beginning of tscs list,
    update pass = 1 after completion
    - calculate mtc from tsc difference between this and next TMA packets.
    - check whether mtc diff between mtc values calculated from tsc difference and from TMA's ctc value differs from
      each other too much (MSB difference) and add MSB to TMA->mtc if needed. Set pass = 1.
  - continue with original iterator forward
  - in case second TMA has pass == 0, fix mtc value according to first TMA
  => OUTPUT: TMA->mtc = valid mtc value
*/
void tsc_heuristics_second_round(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_shift)
{
    tsc_item* first_tma_obj   = nullptr;
    tsc_item* second_tma_obj   = nullptr;

    tscs::iterator it = tscs.begin();

    // ***** Fast-forward to first complete TMA packet (pass == 1) *****
    bool found = false;
    unsigned tma_count = 0;
    for (; it != tscs.end() && !found; it++) {
        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TMA:
            if (t.second.pass) {
                found = true;
            }
            tma_count++;
            break;
        default:
            break;
        }
    }

    // ***** Reverse iterate to beginning (if needed) *****
    if (tma_count > 1) {
        // second_tma_obj pointing to first complete TMA packet
        second_tma_obj = &((*it).second);
        // Set reverse iterator pointing to original iterator (pointing to first complete TMA)
        //&*(reverse_iterator(i)) == &*(i - 1)
        tscs::reverse_iterator rit(it);
        // Go one step towards beginning of the tscs list to skip the current (complete) TMA
        rit++;
        for (; rit != tscs.rend(); rit++) {
            auto& t = *rit;
            switch (t.second.type) {
            case tsc_item_type::TMA:
                if (t.second.tsc) {
                    first_tma_obj = &t.second;
                    adjust_first_tma_mtc_value(first_tma_obj, second_tma_obj);
                    // go to next TMA in reverse iteration
                    second_tma_obj = first_tma_obj;
                }
                break;
            default:
                break;
            }

        }
    }

    // ***** Continue iteration from first complete TMA forward *****
    //  first_tma_obj pointing to first complete TMA
    first_tma_obj = &(*it).second;
    // Continue iteration from next packet in tscs list
    for (it++; it != tscs.end(); it++) {
        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TMA:
            if (t.second.tsc) {
                second_tma_obj = &t.second;
                if (!t.second.pass) {
                    adjust_second_tma_mtc_value(first_tma_obj, second_tma_obj);
                }
                first_tma_obj = second_tma_obj;
            }
            break;
        default:
            break;
        }
    }
}

/*
3. TSCs for MTCs
  - Iterate MTCs between two TMAs:
    if mtc_count < 0xFF:
      if MTC->mtc > second_tma_obj->mtc:
        mtc arrived too early, remove it.
      else:
        Calculate TSC for MTCs:
          MTC->tsc = (MTC->mtc - TMA->mtc)&0x80 * tsc_for_one_mtc()
    else:
      - count mtc "steps-back" points, and compare to mtc_rollover count. If they match, we have exact known
        locations for all mtcs in that TMA-TMA block.
            Set known_rollover_points == 1
*/
void tsc_heuristics_third_round(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_shift)
{
    tsc_item* first_tma_obj   = nullptr;
    tsc_item* second_tma_obj   = nullptr;
    tscs::iterator mtc_remove_it = tscs.end();

    // for case of mutiple MTC rollovers in one TMA-TMA block
    tsc_item* last_mtc     = nullptr;
    unsigned mtc_stepbacks = 0;

    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        // check whether we have MTCs to remove
        if (mtc_remove_it != tscs.end()) {
            tscs.erase(mtc_remove_it);
            mtc_remove_it = tscs.end();
        }

        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TMA:
            {
                // check mtc rollover case
                if (first_tma_obj && second_tma_obj && first_tma_obj->mtc_count >= 0xff) {
                    // we have reached the second_tma_obj
                    if (last_mtc && last_mtc->mtc > t.second.mtc) {
                        mtc_stepbacks++;
                    }
                    unsigned mtc_rollovers = first_tma_obj->mtc_count / 0xff;
                    if (mtc_rollovers == mtc_stepbacks) {
                        // There are equal amount of calculated rollovers and detected backsteps
                        // => all MTCs has exact known location.
                        first_tma_obj->known_rollover_points = 1;
                    }
                }

                first_tma_obj = &t.second;

                // forward next_tma_it to next TMA packet
                second_tma_obj = nullptr;
                tscs::iterator next_tma_it(it);
                for (next_tma_it++;next_tma_it != tscs.end() && (*next_tma_it).second.type != tsc_item_type::TMA;
                     next_tma_it++);
                if (next_tma_it != tscs.end()) {
                    second_tma_obj = &(*next_tma_it).second;
                    //printf("%08lx: TMA tscdiff(0x%lx)\n", t.first, second_tma_obj->tsc - t.second.tsc);
                }
                mtc_stepbacks = 0;

            }
            break;
        case tsc_item_type::MTC:
            if (first_tma_obj && first_tma_obj->pass && second_tma_obj) {
                // valid TSC/TMA pair occurred before this MTC
                if (first_tma_obj->mtc_count < 0xff) {
                    // no full MTC rounds within this TMA2TMA block
                    // Calculate mtc diff from the next mtc after first tma obj.
                    uint8_t next_mtc_from_tma = (first_tma_obj->mtc + 1)&0xff;
                    uint8_t mtcdiff = (t.second.mtc - next_mtc_from_tma)&0xff;
                    // Calculate tsc value for MTC packet
                    t.second.tsc = first_tma_obj->tsc_in_next_mtc + (mtcdiff * tscs_for_one_mtc(tscs_to_ctc, mtc_shift));
                    //t.second.tsc = first_tma_obj->tsc + (((t.second.mtc - first_tma_obj->mtc)&0xff) * tscs_for_one_mtc(tscs_to_ctc, mtc_shift));
                    t.second.pass = 1;
                    //printf("%08lx:   MTC mtc(0x%x), ftma.mtc(0x%x), mtcdiff(0x%x) tsc_diff(0x%lx)\n",
                    //    t.first, t.second.mtc, first_tma_obj->mtc, ((t.second.mtc - first_tma_obj->mtc)&0xff), (((t.second.mtc - first_tma_obj->mtc)&0xff) * tscs_for_one_mtc(tscs_to_ctc, mtc_shift)));
                    if (t.second.tsc > second_tma_obj->tsc) {
                        //printf("#%08lx: ERROR: MTC not align! MTC: mtc(%x),tsc(%lx) > next TMA: mtc(%x),tsc(%lx)\n",
                        //        t.first,t.second.mtc, t.second.tsc, second_tma_obj->mtc, second_tma_obj->tsc);

                        // MTC has arrived too early, mark to be removed.
                        mtc_remove_it = it;
                    }

                } else {
                    // MTC rollovers occurred
                    if (last_mtc) {
                        if (last_mtc->mtc > t.second.mtc) {
                            mtc_stepbacks++;
                        }
                    } else if (first_tma_obj) {
                        if (first_tma_obj->mtc > t.second.mtc) {
                            mtc_stepbacks++;
                        }
                    }
                }
            }
            last_mtc = &t.second;
            break;
        default:
            break;
        }
    }

}

/*
Fix MTCs in TMA-2-TMA block where MTCs has known rollover points.
- Interate all TMAs having known_rollover_points==1
  - Set tsc for MTCs in that TMA-2-TMA block

*/
void tsc_heuristics_fourth_round(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_shift)
{
    tsc_item* first_tma_obj   = nullptr;
    tsc_item* second_tma_obj   = nullptr;
    tscs::iterator mtc_remove_it = tscs.end();


    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        // check whether we have MTCs to remove
        if (mtc_remove_it != tscs.end()) {
            tscs.erase(mtc_remove_it);
            mtc_remove_it = tscs.end();
        }

        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TMA:
            {
                first_tma_obj = &t.second;
                second_tma_obj = nullptr;

                if (first_tma_obj->known_rollover_points) {
                    // forward next_tma_it to next TMA packet
                    tscs::iterator next_tma_it(it);
                    for (next_tma_it++;next_tma_it != tscs.end() && (*next_tma_it).second.type != tsc_item_type::TMA;
                         next_tma_it++);
                    if (next_tma_it != tscs.end()) {
                        second_tma_obj = &(*next_tma_it).second;
                    }
                }
            }
            break;
        case tsc_item_type::MTC:
            if (first_tma_obj && first_tma_obj->known_rollover_points && second_tma_obj) {
                // valid TSC/TMA pair occurred before this MTC
                // Calculate mtc diff from the next mtc after first tma obj.
                uint8_t next_mtc_from_tma = (first_tma_obj->mtc + 1)&0xff;
                uint8_t mtcdiff = (t.second.mtc - next_mtc_from_tma)&0xff;
                // Calculate tsc value for MTC packet
                t.second.tsc = first_tma_obj->tsc_in_next_mtc + (mtcdiff * tscs_for_one_mtc(tscs_to_ctc, mtc_shift));
                t.second.pass = 2;
                if (t.second.tsc > second_tma_obj->tsc) {
                    printf("#%08lx: ERROR: MTC not align! MTC: mtc(%x),tsc(%lx) > next TMA: mtc(%x),tsc(%lx)\n",
                            t.first,t.second.mtc, t.second.tsc, second_tma_obj->mtc, second_tma_obj->tsc);

                    // MTC has arrived too early, mark to be removed.
                    mtc_remove_it = it;
                }
            } else if (first_tma_obj && !second_tma_obj && !t.second.pass ) {
                // we are in the end part of file, after last TMA packet.
                // Calculate mtc diff from the next mtc after first tma obj.
                uint8_t next_mtc_from_tma = (first_tma_obj->mtc + 1)&0xff;
                uint8_t mtcdiff = (t.second.mtc - next_mtc_from_tma)&0xff;
                // Calculate tsc value for MTC packet
                t.second.tsc = first_tma_obj->tsc_in_next_mtc + (mtcdiff * tscs_for_one_mtc(tscs_to_ctc, mtc_shift));
                t.second.pass = 3;
            }
            break;
        default:
            break;
        }
    }

}

/*
Fill MTC timestamps till the end of the trace
*/
void tsc_heuristics_fifth_round(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_shift)
{

}


// first pass heuristics
#if 0
void assign_tscs_for_mtcs_after_tsctmas(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    tsc_item* tma_obj   = nullptr;
    tsc_item* tsc_obj   = nullptr;
    bool      have_tsc  = false;
    uint64_t  tsc       = 0;
    uint8_t   mtc       = 0;
    tsc_item* mtc_obj   = nullptr;
    tsc_item_type last_type = PSB;

    tscs::iterator prev_mtc_iter = tscs.end();

    //  Use ordinary for loop to be able to remove MTC objects from map
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc_obj = &t.second;
            tsc       = t.second.tsc;
            have_tsc  = true;
            // Remove last mtc if:
            //   1. MTC time is higher than TSC, meaning it should have arrived
            //      rigth after the TSC.
            //   2. MTC was last packet before TSC so
            //      there is no execution in between.
            if (prev_mtc_iter != tscs.end()) {
                if (((*prev_mtc_iter).second.tsc > t.second.tsc) ||
                    ((*prev_mtc_iter).first + 2 == t.first)) {
                    // remove last MTC all together
                    tscs.erase(prev_mtc_iter);
                }
                prev_mtc_iter = tscs.end();
            }
            last_type = t.second.type;
            break;
        case tsc_item_type::TMA:
            if (have_tsc) {
                uint64_t tsc_delta = 0;
                // tsc delta between tma's:
                if (tma_obj) {
                    tsc_delta = tsc - tma_obj->tsc;
                }
                // TSC packet arrived before TMA
                tma_obj = &t.second;
                tma_obj->tsc = tsc;
                // Calculate in which mtc range the TSC/TMA pair occurs.
                tma_obj->mtc = ((tma_obj->ctc) >> mtc_freq) & 0xFF;
                // If needed, add MSB bit from real MTC counter
                //   into artificial mtc value of TSC generated
                //   from TMA's ctc count.
                if (mtc_obj) {
                    // Check whether TMA's mtc is in next round compared to previous MTC packet.
                    //  Ignore MSB bit of MTC as it is not visible in artificial mtc count calculated from
                    //  16-bit CTC counter.
                    //  Also, the previous MTC may have arrived too early, so the counter value
                    //  can be one tick bigger by accident. To get over that, ignore also lower 2 bits.
                    if (tma_obj->mtc < (mtc&0x7C)) {
                        printf("%08lx: tmamtc:%x => clear MSB bit (last MTC:%x)\n", t.first, tma_obj->mtc, mtc);
                        // If 7 LSB bits of previous MTC are greater than current one,
                        //  it has probably just rolled-over, so remove MSB bit.
                        tma_obj->mtc &= 0x7F;
                    } else {
                        printf("%08lx: tmamtc:%x => copy last mtc MSB bit (last MTC: %x)\n", t.first, tma_obj->mtc, mtc);
                        // Otherwise, just copy the MSB from last mtc.
                        tma_obj->mtc |= (mtc&0x80);
                    }
                }
                // The ctc count value after TMA packet has arrived and amount of TSC ticks
                //   given by 'fast' field are elapsed.
                uint32_t next_ctc_from_tma = (tma_obj->ctc + 1)&0xFFFF;
                // Calculated ctc count value for the moment when next MTC will arrive.
                uint32_t ctc_ticks_to_next_mtc_packet =
                        ((( ((tma_obj->mtc+1)&0xFF) << mtc_freq)&0xFFFF) - next_ctc_from_tma)&0xFFFF;
                // Calculated tsc tick value for the moment when next MTC will arrive.
                tma_obj->tsc_in_next_mtc = tma_obj->tsc + tma_obj->fast + (ctc_ticks_to_next_mtc_packet * tscs_to_ctc);

                if(tsc_obj) {
                    tsc_obj->mtc = tma_obj->mtc;
                    tsc_obj->tsc_in_next_mtc = tma_obj->tsc_in_next_mtc;
                    tsc_obj = nullptr;
                }

            }
            last_type = t.second.type;
            break;

        case tsc_item_type::MTC:
            mtc_obj = &t.second;
            //printf("%08lx: MTC: begin (ctc:%x)\n", t.first, mtc_obj->mtc);
            if (have_tsc) {
                if (last_type == tsc_item_type::TMA) {
                    // Last timing packet was TMA, so get tsc value from there
                    tsc = tma_obj->tsc_in_next_mtc;
                    mtc_obj->tsc = tsc;
                    mtc_obj->pass = 1;
                    //printf("%08lx: MTC(1) : %x => %lx (ctc_cnt: %x, ctcspent: %x, tscs_to_ctc: %x)\n", t.first, mtc_obj->mtc, mtc_obj->tsc, (uint16_t)(mtc_obj->mtc << mtc_freq), ctc_ticks_from_last_timing_packet, tscs_to_ctc);
                } else if (last_type == tsc_item_type::MTC) {
                    // Last timing packet was MTC, check whether it was previous in line
                    //   without any gaps in between. In case there were gaps, leave it for
                    //   further handling.
                    if (mtc_obj->mtc == ((mtc+1)&0xFF)) {
                        tsc = tsc + tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                        mtc_obj->tsc = tsc;
                        mtc_obj->pass = 1;
                        //printf("%08lx: MTC(2) : %x => %lx\n", t.first, mtc_obj->mtc, mtc_obj->tsc);
                    }
                }
                prev_mtc_iter = it;
                //printf("MTC: tsc = %lx\n", t.second.tsc);

            }
            last_type = t.second.type;
            mtc = mtc_obj->mtc;
            break;

        case tsc_item_type::OVF:
            t.second.tsc = 0;
            have_tsc  = false;
            last_type = t.second.type;
            break;

        default:
            break;
        }
    }
}

// second pass
void assign_tscs_for_mtcs_before_tsctmas(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    tsc_item* tsc_obj   = nullptr;
    bool      have_tsc  = false;
    uint64_t  tsc       = 0;
    uint8_t   mtc       = 0;
    tsc_item* mtc_obj   = nullptr;
    tsc_item_type last_type = PSB;

    //  Go through tscs list in reverse order
    for (tscs::reverse_iterator rit = tscs.rbegin(); rit != tscs.rend(); rit++) {
        auto& t = *rit;
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc_obj = &t.second;
            tsc       = t.second.tsc;
            //printf("%08lx: TSC : %lx\n", t.first, tsc);
            have_tsc  = true;
            last_type = t.second.type;
            break;

        case tsc_item_type::MTC:
            mtc_obj = &t.second;

            if (!mtc_obj->tsc) {
                // MTC did not have tsc timestamp
                if (have_tsc) {
                    //printf("%08lx: MTC: begin (mtc:%x) tsc-mtc:%x lasttype:%d\n", t.first, mtc_obj->mtc, tsc_obj->mtc, last_type);
                    if (last_type == tsc_item_type::TSC &&
                        tsc_obj &&
                        mtc_obj->mtc == tsc_obj->mtc) {
                            // No gaps between previous TSC and current MTC
                            // Calculate tsc for previous mtc packet
                            tsc = tsc_obj->tsc_in_next_mtc - tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                            mtc_obj->tsc = tsc;
                            mtc_obj->pass = 2;
                            //printf("%08lx: MTC(2) : %x => %lx\n", t.first, mtc_obj->mtc, mtc_obj->tsc);
                    } else if (last_type == tsc_item_type::MTC) {
                        if (mtc_obj->mtc == ((mtc-1)&0xff)) {
                            // No gaps between previous MTC and current MTC
                            // Calculate tsc for previous mtc packet
                            tsc = tsc - tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                            mtc_obj->tsc = tsc;
                            mtc_obj->pass = 2;
                            //printf("%08lx: MTC(2) : %x => %lx\n", t.first, mtc_obj->mtc, mtc_obj->tsc);
                        }
                    }
                    //printf("MTC: tsc = %lx\n", t.second.tsc);
                }
            }
            mtc = mtc_obj->mtc;
            last_type = t.second.type;
            break;

        case tsc_item_type::OVF:
            t.second.tsc = 0;
            have_tsc  = false;
            last_type = t.second.type;
            break;

        default:
            break;
        }
    }
}


void assign_tscs_for_mtcs_having_gaps(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    uint64_t  tsc       = 0;
    tsc_item* last_complete_mtc   = nullptr;
    tscs::iterator mtc_fix_it = tscs.end();

    // Iterate tscs list from begin to end
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        if (mtc_fix_it != tscs.end() && t.second.tsc) {
            // End of mtc gap reached, check whether there is possibility to
            //  assign tscs for those mtcs.
            int rollovers = (t.second.tsc - tsc) /
                            tscs_for_mtc_rollover(tscs_to_ctc, mtc_freq);
            if (!rollovers) {
                // MTC counter has not rolled over, so iterate through gap and assign tscs
                for (; mtc_fix_it != tscs.end() && mtc_fix_it != it;) {
                    auto& mt = *mtc_fix_it;
                    switch (mt.second.type) {
                    case tsc_item_type::MTC:
                        {
                            auto mtc_obj = &mt.second;
                            // calculate gap as amount of MTC counts
                            int gap = mtc_obj->mtc - last_complete_mtc->mtc;
                            // calculate tsc for next mtc using the last known MTC stamp
                            uint64_t tmp_tsc = last_complete_mtc->tsc + gap * tscs_for_one_mtc(tscs_to_ctc,mtc_freq);

			    // Remove last mtc if:
			    //   1. MTC time is higher than next valid timing packet TSC, meaning it
                            //      should have arrived right after the next packet.
                            printf("%08lx: MTC tsc:%lx --> next TSC: %lx\n",  mt.first, tmp_tsc, t.second.tsc);
                            if (tmp_tsc > t.second.tsc) {
                                // remove current MTC all together
                                auto prev_mtc_iter = mtc_fix_it;
                                mtc_fix_it++;
                                tscs.erase(prev_mtc_iter);
                                continue;
                            } else {
			        tsc = tmp_tsc;
                                mtc_obj->tsc = tsc;
                                // printf("%08lx: gap:%d, TSC : %lx mtc:%x (lmtc=%x, tsc:%lx)\n",
                                //         mt.first, gap, tsc, mtc_obj->mtc, last_complete_mtc->mtc, last_complete_mtc->tsc);
                                mtc_obj->pass = 3;
                                // Set this MTC as last known mtc as it now has proper stamp already.
                                last_complete_mtc = mtc_obj;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                    mtc_fix_it++;
                }
            }
            // Clear mtc fixin iterator
            mtc_fix_it = tscs.end();
        }
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc       = t.second.tsc;
            break;

        case tsc_item_type::MTC:
            {
                auto mtc_obj = &t.second;
                if (mtc_obj->tsc > 0) {
                    // MTC with proper timestamp found, set it as last known mtc
                    last_complete_mtc = mtc_obj;
                    tsc = t.second.tsc;
                } else if (mtc_fix_it == tscs.end()){
                    // MTC without timestamp found, set mtc_fix iterator only if
                    //  iterator is not already active.
                    mtc_fix_it = it;
                }
            }
            break;
        default:
            break;
        }
    }
}

void assign_tscs_for_ovf(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    tsc_item* tp_before = nullptr;
    tsc_item* tp_after = nullptr;
    tsc_item* last_ovf = nullptr;
    tscs::iterator ovf_fix_it = tscs.end();
    // Iterate tscs list from begin to end
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        switch (t.second.type) {
        case tsc_item_type::MTC:
        case tsc_item_type::TSC:
            if (t.second.tsc > 0) {
                if (ovf_fix_it != tscs.end()) {
                    // Overflow fix iterator active, start fixing..
                    if (tp_before != nullptr) {
                        // There was timestamp packet before the first overflow in a row
                        tp_after = &t.second;
                        auto mtc_b = tp_before->mtc; // Timestamp before the set of overflows
                        auto mtc_a = tp_after->mtc;  // Timestamp after the set of oveflows
                        if (tp_after->type == tsc_item_type::TSC) {
                            // If latter timestamp is TSC, the mtc is pointing to previously output mtc, so
                            //   for comparison we need to increment mtc_after value by one.
                            mtc_a = (mtc_a+1) & 0xFF;
                        }
                        if ((mtc_a & 0xFF) == ((mtc_b + 1) & 0xFF)) {
                            // the timing packets before and after the set of overflows are consecutive
                            //  timing packets, so set the mtc_before timestamp for all the overflows in
                            //  the set.
                            for (; ovf_fix_it != tscs.end() && ovf_fix_it != it; ovf_fix_it++) {
                                (*ovf_fix_it).second.tsc = tp_before->tsc;
                            }
                        } else if (last_ovf) {
                            // There are gaps between timing packets before and after the overflow set,
                            //   assign timestmap only for the last ovf before the valid timing packet.
                            switch (tp_after->type) {
                            case tsc_item_type::MTC:
                                last_ovf->tsc = tp_after->tsc - tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                                break;
                            case tsc_item_type::TSC:
                                last_ovf->tsc = tp_after->tsc_in_next_mtc - tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                                break;
                            default:
                                break;
                            }
                            last_ovf = nullptr;
                        }
                    }
                    // Clear overflow fix iterator
                    ovf_fix_it = tscs.end();
                }
                if (ovf_fix_it == tscs.end()) {
                    // In case there are no pending overflows to be fixed, set
                    //  current TSC/MTC as a latest timestamp packet before overflow.
                    tp_before = &t.second;
                }
            }
            break;
        case tsc_item_type::OVF:
            if (ovf_fix_it == tscs.end()) {
                // Overflow found and there are no pending overflows to be fixed, so
                //  set overflow fix iterator
                ovf_fix_it = it;
            }
            // Set last overflow pointer in case we are able to fix only the last
            //  one from the set of overflows (in case there are gap between timestamps
            //  before and after the set of overflows)
            last_ovf = &t.second;
            break;
        default:
            break;
        }
    }
}


void assign_tscs_for_psb(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    uint64_t tsc = 0;
    // Iterate tscs list from begin to end
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;

        switch (t.second.type) {
        case tsc_item_type::MTC:
        case tsc_item_type::TSC:
        case tsc_item_type::OVF:
            tsc = t.second.tsc;
            break;
        case tsc_item_type::PSB:
            // PSB found, take timestamp from previous timing packet
            //  or last overflow point
            t.second.tsc = tsc;
            break;
        default:
            break;
        }
    }
}
#endif

#if 0
void assign_tscs_for_mtcs_having_gaps(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    uint64_t  tsc       = 0;
    tsc_item* last_complete_mtc   = nullptr;
    tscs::iterator mtc_fix_it = tscs.end();
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        if (t.second.tsc > 0) {
            if (mtc_fix_it != tscs.end()) {
                int rollovers = (t.second.tsc - tsc) /
                                tscs_for_mtc_rollover(tscs_to_ctc, mtc_freq);
                if (!rollovers) {
                //Iterate through gap
                    for (; mtc_fix_it != tscs.end() && mtc_fix_it != it; mtc_fix_it++) {
                        auto& mt = *mtc_fix_it;
                        switch (mt.second.type) {
                        case tsc_item_type::MTC:
                            {
                                auto mtc_obj = &mt.second;
                                int gap = mtc_obj->mtc - last_complete_mtc->mtc;
                                mtc_obj->tsc = last_complete_mtc->tsc + gap * ((1<<mtc_freq)*tscs_to_ctc);
                                last_complete_mtc = mtc_obj;
                            }
                            break;
                        default:
                            break;
                        }
                    }
                } else {
                    //roll-over occurred.
                }
                mtc_fix_it = tscs.end();
            }
        }

        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc = t.second.tsc;
            if (last_complete_mtc && ((last_complete_mtc->mtc&0x7F) < (t.second.mtc&0x7F))) {
                t.second.mtc |= last_complete_mtc->mtc&0x80;
            }
            break;
        case tsc_item_type::MTC:
            if (t.second.tsc) {
                tsc = t.second.tsc;
                last_complete_mtc = &t.second;
            } else {
                if (mtc_fix_it == tscs.end()) {
                    mtc_fix_it = it;
                }
            }
            break;
        default:
            break;
        }
    }
}
#endif

#if 0
void assign_tscs_for_ovf_psb(tscs& tscs, uint32_t tscs_to_ctc, uint8_t mtc_freq)
{
    tsc_item* last_ovf_obj        = nullptr;
    tsc_item* last_psb_obj        = nullptr;
    uint64_t  tsc = 0;
    for (tscs::iterator it = tscs.begin(); it != tscs.end(); it++) {
        auto& t = *it;
        if(t.second.tsc > 0) {
            switch (t.second.type) {
            case tsc_item_type::MTC:
                if(last_ovf_obj || last_psb_obj) {
                    uint64_t obj_tsc;
                    if(t.second.tsc - tsc > tscs_for_one_mtc(tscs_to_ctc,mtc_freq)) {
                        obj_tsc = t.second.tsc - tscs_for_one_mtc(tscs_to_ctc,mtc_freq);
                    } else {
                        obj_tsc = tsc;
                    }
                    if (last_ovf_obj) {
                        last_ovf_obj->tsc = obj_tsc;
                        last_ovf_obj = nullptr;
                    }
                    if (last_psb_obj) {
                        last_psb_obj->tsc = obj_tsc;
                        last_psb_obj = nullptr;
                    }
                }
                tsc = t.second.tsc;
                break;
            case tsc_item_type::TSC:
                if(last_ovf_obj || last_psb_obj) {
                    uint64_t obj_tsc;
                    if(t.second.tsc - tsc > tscs_for_one_mtc(tscs_to_ctc,mtc_freq)) {
                        obj_tsc = t.second.tsc_in_last_mtc;
                    } else {
                        obj_tsc = tsc;
                    }
                    if (last_ovf_obj) {
                        last_ovf_obj->tsc = obj_tsc;
                        last_ovf_obj = nullptr;
                    }
                    if (last_psb_obj) {
                        last_psb_obj->tsc = obj_tsc;
                        last_psb_obj = nullptr;
                    }
                }
                if(last_psb_obj) {
                    if(t.second.tsc - tsc > tscs_for_one_mtc(tscs_to_ctc,mtc_freq)) {
                        last_psb_obj->tsc = t.second.tsc_in_last_mtc;
                    } else {
                        last_psb_obj->tsc = tsc;
                    }
                    last_psb_obj = nullptr;
                }
                tsc = t.second.tsc;
                break;
            default:
                break;
            }
        } else {
            switch (t.second.type) {
            case tsc_item_type::OVF:
                last_ovf_obj = &t.second;
                if (last_psb_obj) {
                    last_psb_obj = nullptr;
                }
                break;
            case tsc_item_type::PSB:
                last_psb_obj = &t.second;
                break;
            default:
                break;
            }
        }
    }
}
#endif
#if 0 // TMA can affect MTC between TSC and TMA -- this is what tests indicate
void assign_tscs_for_mtcs_after_tsctmas(tscs& tscs)
{
    bool      have_fast = false;
    uint64_t  fast      = 0;
    bool      have_tsc  = false;
    uint64_t  tsc       = 0;
    bool      have_mtc  = false;
    tsc_item* mtc       = nullptr;

    for (auto& t : tscs) {
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc       = t.second.tsc;
            have_tsc  = true;
            have_fast = false;
            have_mtc  = false;
            break;
        case tsc_item_type::TMA:
            if (have_tsc) {
                fast      = t.second.fast;
                have_fast = true;
            }
            break;
        case tsc_item_type::MTC:
            if (have_tsc && !have_mtc) {
                mtc      = &t.second;
                have_mtc = true;
            }
            break;
        case tsc_item_type::OVF:
            have_tsc  = false;
            have_fast = false;
            have_mtc  = false;
            break;
        case tsc_item_type::PSB:
            have_tsc  = false;
            have_fast = false;
            have_mtc  = false;
            break;
        }
        if (have_tsc && have_fast && have_mtc) {
            mtc->tsc  = tsc + fast;
            mtc->pass = 1;
            have_tsc  = false;
            have_fast = false;
            have_mtc  = false;
        }
    }
}
#endif
#if 0 // TMA only affects the MTC following it -- tests indicate otherwise
void assign_tscs_for_mtcs_after_tsctmas(tscs& tscs)
{
    bool     have_fast = false;
    uint64_t fast      = 0;
    bool     have_tsc  = false;
    uint64_t tsc       = 0;

    for (auto& t : tscs) {
        switch (t.second.type) {
        case tsc_item_type::TSC:
            tsc       = t.second.tsc;
            have_tsc  = true;
            have_fast = false;
            break;
        case tsc_item_type::TMA:
            fast      = t.second.fast;
            have_fast = true;
            break;
        case tsc_item_type::MTC:
            if (have_tsc && have_fast) {
                t.second.tsc  = tsc + fast;
                t.second.pass = 1;
                have_tsc  = false;
                have_fast = false;
            }
            break;
        case tsc_item_type::OVF:
            have_tsc  = false;
            have_fast = false;
            break;
        }
    }
}
#endif

// second pass heuristics: fill tsc values for MTCs between two tsc values
void fill_mtc_tscs(tscs::iterator        from,
                   const tscs::iterator& to,
                   uint64_t              mtc_delta)
{
    uint64_t tsc_delta = to->second.tsc - from->second.tsc;
    uint64_t t         = from->second.tsc;
    uint64_t md        = 0;
    int      prev_mtc  = from->second.mtc;

    while (++from != to) {
        if (from->second.type == tsc_item_type::MTC) {
            // accumulate MTC from zero towards mtc_delta
            md += adjacent_mtc_delta(prev_mtc, from->second.mtc);
            // count TSC based on accumulated MTC
            from->second.tsc = t + tsc_delta * md / mtc_delta;
            from->second.pass = 2;
            // save MTC for the next iteration
            prev_mtc = from->second.mtc;
        }
    }
}

void fill_mtc_tscs(tsc_block& block, uint64_t tsc_mtc_ratio, uint64_t mtc_delta)
{
    fill_mtc_tscs(block.mtc.first, block.mtc.second, mtc_delta);
}

void fill_mtc_tscs(tsc_collection& collection, uint64_t tsc_mtc_ratio)
{
    for (auto& block : collection.blocks) {
        uint64_t mtc_delta;
        if (get_mtc_delta(block, tsc_mtc_ratio, mtc_delta)) {
            fill_mtc_tscs(block, tsc_mtc_ratio, mtc_delta);
        }
    }
}

// third & fourth pass heuristics
void project_mtc_tscs_into_future(tscs::iterator        from,
                                  const tscs::iterator& to,
                                  uint64_t              tsc_mtc_ratio)
{
    auto t = from->second.tsc;
    auto m = from->second.mtc;

    while (from != to) {
        ++from;
        if (from->second.type == tsc_item_type::MTC) {
            t += adjacent_mtc_delta(m, from->second.mtc) * tsc_mtc_ratio;
            m = from->second.mtc;
            from->second.tsc  = t;
            from->second.pass = 3;
        }
    }
}

void project_mtc_tscs_into_past(tscs::iterator        from,
                                const tscs::iterator& to,
                                uint64_t              tsc_mtc_ratio)
{
    auto t = from->second.tsc;
    auto m = from->second.mtc;

    while (from != to) {
        --from;
        if (from->second.type == tsc_item_type::MTC) {
            t -= adjacent_mtc_delta(from->second.mtc, m) * tsc_mtc_ratio;
            m = from->second.mtc;
            from->second.tsc  = t;
            from->second.pass = 4;
        }
    }
}

void fill_broken_block_ends(tsc_collection& collection, uint64_t tsc_mtc_ratio)
{
    if (collection.has_head) {
        project_mtc_tscs_into_past(collection.head.mtc.second,
                                    collection.head.mtc.first,
                                    tsc_mtc_ratio);
    }

    for (auto& block : collection.blocks) {
        uint64_t dummy;
        if (!get_mtc_delta(block, tsc_mtc_ratio, dummy)) {
            auto& first_gap_start = block.gaps[0].first;
            project_mtc_tscs_into_future(block.mtc.first,
                                         first_gap_start,
                                         tsc_mtc_ratio);
            auto& last_gap_end = block.gaps[block.gaps.size() - 1].second;
            project_mtc_tscs_into_past(block.mtc.second,
                                       last_gap_end,
                                       tsc_mtc_ratio);
        }
    }

    if (collection.has_tail) {
        project_mtc_tscs_into_future(collection.tail.mtc.first,
                                     collection.tail.mtc.second,
                                     tsc_mtc_ratio);
    }
}

bool is_sane(const tscs& tscs, ipt_pos& insanity)
{
    uint64_t tsc = 0;
    for (const auto& t : tscs) {
        if (t.second.type == tsc_item_type::MTC) {
            if (t.second.pass == 0) {
                //printf("#MTC HAS NO TSC AT %08lx\n", t.first);
            } else if (t.second.tsc && t.second.tsc <= tsc) {
                insanity = t.first;
                printf("#TSC NOT SANE AT %08lx (last_tsc %08lx > tsc %08lx)\n", insanity, tsc, t.second.tsc);
                return false;
            } else {
                tsc = t.second.tsc;
            }
        }
    }

    return true;
}

#if 0
void fill_between_mtcs_with_tscs(tscs& tscs)
{
    bool           have_mtc_with_tsc = false;
    tscs::iterator mtc_with_tsc;
    uint64_t       mtc_delta         = 0;
    int            prev_mtc          = 0;

    for (auto t = tscs.begin(); t != tscs.end(); ++t) {
        switch (t->second.type) {
        case tsc_item_type::MTC:
            if (have_mtc_with_tsc) {
                // accumulate MTC delta
                mtc_delta += adjacent_mtc_delta(prev_mtc, t->second.mtc);
            }
            if (t->second.pass != 0) {
                if (have_mtc_with_tsc) {
                    fill_between_mtcs_with_tscs(mtc_with_tsc, t, mtc_delta);
                }
                mtc_with_tsc      = t;
                have_mtc_with_tsc = true;
                mtc_delta         = 0;
            }
            prev_mtc = t->second.mtc;
            break;
        case tsc_item_type::OVF:
            have_mtc_with_tsc = false;
            break;
        }
    }
}
#endif

} // anonymous namespace


template <class INPUT>
class collect_timing_packets :
    public ipt_parser_output_base<collect_timing_packets<INPUT>>
{
public:
    using token = ipt_parser_token<collect_timing_packets<INPUT>>;

    collect_timing_packets(const INPUT& input) :
        got_to_eof(), input_(input), tscs_(new tscs), strts_(new strts),
        wait_for_tma_(false), in_psb_(false)
    {
        // cannot put BEGIN at offset 0, because 0 might be occupied by MTC/TSC
        //tscs_->insert({0, {tsc_item_type::BEGIN, -1, 0}});
    }

    void tsc(token& t)
    {
        tscs_->insert({input_.beginning_of_packet(),
                       {tsc_item_type::TSC, 0, t.tsc, 0, 0, 0, 0, 0, false}});
        wait_for_tma_ = true;
    }

    void mtc(token& t)
    {
        // Skip MTCs tht comes between tsc and tma.
        if (!wait_for_tma_ && !in_psb_) {
            tscs_->insert({input_.beginning_of_packet(), {tsc_item_type::MTC, t.ctc, MAX_TSC_VAL, 0, 0, 0, 0, 0, false}});
        }
    }

    void tma(token& t)
    {
        tscs_->insert({input_.beginning_of_packet(),
                       {tsc_item_type::TMA, 0, MAX_TSC_VAL, t.tma.ctc, t.tma.fast, 0, 0, 0, false}});
        wait_for_tma_ = false;
    }

    void ovf(token& t)
    {
        tscs_->insert({input_.beginning_of_packet(), {tsc_item_type::OVF, 0, MAX_TSC_VAL, 0, 0, 0, 0, 0, false}});
        strts_->insert({(ipt_pos)input_.beginning_of_packet(), tsc_item_type::OVF});
    }

    void eof(token& t)
    {
        got_to_eof = true;
    }

    void psb(token& t)
    {
        //tscs_->insert({input_.beginning_of_packet(), {tsc_item_type::PSB, 0, MAX_TSC_VAL, 0, 0, 0, 0, 0, false}});
        strts_->insert({(ipt_pos)input_.beginning_of_packet(), tsc_item_type::PSB});
        in_psb_ = true;
    }
    void psbend(token& t)
    {
        in_psb_ = false;
    }

    void tip_pge(token& t)
    {
        strts_->insert({(ipt_pos)input_.beginning_of_packet(), tsc_item_type::PGE});
    }

    void report_error(const string& message)
    {
        fprintf(stderr, "error parsing IPT: %s\n", message.c_str());
    }

    shared_ptr<tscs> timing_packets()
    {
        return tscs_;
    }

    shared_ptr<strts> start_locations()
    {
        return strts_;
    }
    bool got_to_eof;

private:
    const INPUT&      input_;
    shared_ptr<tscs>  tscs_;
    shared_ptr<strts> strts_;
    bool              wait_for_tma_;
    bool              in_psb_;
}; // collect_timing_packets


struct tsc_heuristics::imp {
    shared_ptr<tscs> tscs_;
    shared_ptr<strts> strts_;
    shared_ptr<const sideband_info> sideband_;
}; // tsc_heuristics::imp

tsc_heuristics::tsc_heuristics(const std::string& sideband_path) :
    imp_{new imp}
{
    shared_ptr<sideband_info> sideband{new sideband_info};
    if (!sideband->build(sideband_path)) {
        exit(EXIT_FAILURE);
    }
    imp_->sideband_ = sideband;
}

tsc_heuristics::~tsc_heuristics() {}

bool tsc_heuristics::parse_ipt(const std::string& path)
{
    bool ok = true;

    ipt_parser<input_from_file, collect_timing_packets> parser;
    ok = parser.input().open(path);

    if (ok) {
        while (parser.parse()) {}
        ok = parser.output().got_to_eof;
    }

    if (ok) {
        imp_->tscs_ = parser.output().timing_packets();
        imp_->strts_ = parser.output().start_locations();
    }

    return ok;
}

void tsc_heuristics::apply()
{
    uint8_t mtc_freq = imp_->sideband_->mtc_freq();
    uint32_t tsc_ctc_ratio = imp_->sideband_->tsc_ctc_ratio();

    tsc_heuristics_first_round(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);
    tsc_heuristics_second_round(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);
    tsc_heuristics_third_round(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);
    tsc_heuristics_fourth_round(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);
#if 0
    // PASS 1:
    // Assing timestamps for MTC's after TSC/TMA pair.
    assign_tscs_for_mtcs_after_tsctmas(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);

    // PASS 2:
    // Assing timestamps for MTC's after TSC/TMA pair.
    assign_tscs_for_mtcs_before_tsctmas(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);

    // PASS 3:
    // Try to find timestamps for MTC's within gaps
    assign_tscs_for_mtcs_having_gaps(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);

    // PASS 4:
    // Assign tsc for OVF where possible
    assign_tscs_for_ovf(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);

    // PASS 5:
    // Assign tscs for PSBs
    assign_tscs_for_psb(*imp_->tscs_, tsc_ctc_ratio, mtc_freq);
#endif
    tsc_collection collection;
    build_tsc_collection(*imp_->tscs_, collection);

    //printf("#%d head block(s)\n", (int)collection.has_head);
    //if (collection.has_head) {
    //    dump_block(collection.head);
    //}
    //printf("#%ld normal blocks\n", collection.blocks.size());
    //printf("#%d tail block(s)\n", collection.has_tail);
    //if (collection.has_tail) {
    //    dump_block(collection.tail);
    //}

    //detect_mtc_gaps(collection);
    // TODO: Get tsc_mtc_ratio from sideband
    //auto r = median_tsc_mtc_ratio(collection);
    //auto r = (tsc_ctc_ratio * (1<<mtc_freq));
    //printf("#tsc / ctc  : %u\n", tsc_ctc_ratio);
    //printf("#mtcFreq    : %u\n", mtc_freq);
    //printf("#tsc / mtc  : %lu\n", (uint64_t) r);

#if 0
    auto& block0 = collection.blocks[0];
    printf("#block 0 [%lx .. %lx):\n",
          block0.mtc.first->second.tsc, block0.mtc.second->second.tsc);
    printf("#block 0 total MTCS: %d\n", total_mtcs(block0));
    printf("#block 0 gaps      : %ld\n", block0.gaps.size());
    printf("#block 0 min gaps  : %d\n", total_minimum_mtc_gaps(block0));
    printf("#block 0 max MTCs  : %d\n", estimated_max_mtcs(block0, r));

    uint64_t dummy;

    tsc_block* gappy = nullptr;
    int        g     = 0;
    for (auto& b : collection.blocks) {
        if (b.gaps.size()) {
            gappy = &b;
            break;
        }
        ++g;
    }
    if (gappy) {
        printf("#block %d [%lx .. %lx) has gaps:\n",
               g, gappy->mtc.first->second.tsc, gappy->mtc.second->second.tsc);
        printf("#    total MTCS: %d\n", total_mtcs(*gappy));
        printf("#    gaps      : %ld\n", gappy->gaps.size());
        printf("#    min gaps  : %d\n", total_minimum_mtc_gaps(*gappy));
        printf("#    max MTCs  : %d\n", estimated_max_mtcs(*gappy, r));
        printf("#    %s good for spreading MTCs\n",
               get_mtc_delta(*gappy, r, dummy) ? "is" : "is NOT");
    }
    gappy = nullptr;
    g     = 0;
    for (auto& b : collection.blocks) {
        if (!get_mtc_delta(b, r, dummy)) {
            gappy = &b;
            break;
        }
        ++g;
    }
    if (gappy) {
        printf("#block %d [%lx .. %lx) is not good for spreading MTCs:\n",
               g, gappy->mtc.first->second.tsc, gappy->mtc.second->second.tsc);
        printf("#    total MTCS: %d\n", total_mtcs(*gappy));
        printf("#    gaps      : %ld\n", gappy->gaps.size());
        printf("#    min gaps  : %d\n", total_minimum_mtc_gaps(*gappy));
        printf("#    max MTCs  : %d\n", estimated_max_mtcs(*gappy, r));
        printf("#    %s good for spreading MTCs\n",
               get_mtc_delta(*gappy, r, dummy) ? "is" : "is NOT");
    }

    gappy = nullptr;
    g     = 0;
    for (auto& b : collection.blocks) {
        if (b.gaps.size() > 2) {
            gappy = &b;
            break;
        }
        ++g;
    }

    if (gappy) {
        printf("#block %d [%lx .. %lx) has more than 2 gaps:\n",
               g, gappy->mtc.first->second.tsc, gappy->mtc.second->second.tsc);
        printf("#    total MTCS: %d\n", total_mtcs(*gappy));
        printf("#    gaps      : %ld\n", gappy->gaps.size());
        printf("#    min gaps  : %d\n", total_minimum_mtc_gaps(*gappy));
        printf("#    max MTCs  : %d\n", estimated_max_mtcs(*gappy, r));
        printf("#    %s good for spreading MTCs\n",
               get_mtc_delta(*gappy, r, dummy) ? "is" : "is NOT");
    }

    int good_blocks = 0;
    for (auto& b : collection.blocks) {
        if (get_mtc_delta(b, r, dummy)) ++good_blocks;
    }
    printf("#%d/%ld blocks are good\n", good_blocks, collection.blocks.size());

    int repairable_blocks = 0;
    for (auto& b : collection.blocks) {
        if (b.gaps.size() == 1 && !get_mtc_delta(b, r, dummy)) {
            ++repairable_blocks;
            break;
        }
        ++g;
    }
    printf("#%d blocks are repairable\n", repairable_blocks);
#endif
    //fill_mtc_tscs(collection, r);
    //printf("#fill_broken_blocks\n");
    //fill_broken_block_ends(collection, r);

    ipt_pos insanity = 0;
    if (is_sane(*imp_->tscs_, insanity)) {
       printf("# tsc heuristics sane\n");
    } else {
       printf("#TSC NOT SANE AT %08lx\n", insanity);
    }
}

bool get_next_tsc(const tscs& tscs, tscs::const_iterator& t, uint64_t& tsc)
{
    bool got_it = false;

    uint64_t current_tsc = t->second.tsc;

    if (current_tsc != 0) {
        for (++t; t != tscs.end(); ++t) {
            if (t->second.tsc != 0 && t->second.tsc > current_tsc) {
                tsc = t->second.tsc;
                got_it = true;
                break;
            }
        }
    }

    return got_it;
}

bool get_prev_tsc(const tscs& tscs, tscs::const_iterator& t, uint64_t& tsc)
{
    bool got_it = false;

    uint64_t current_tsc = t->second.tsc;

    if (current_tsc != 0) {
        for (--t; t != tscs.begin(); --t) {
            if (t->second.tsc != 0 && t->second.tsc < current_tsc) {
                tsc = t->second.tsc;
                got_it = true;
                break;
            }
        }
    }

    return got_it;
}

bool tsc_heuristics::get_tsc(ipt_pos pos, uint64_t& tsc, uint64_t& next_tsc)
{
    bool got_it = false;

    auto i = imp_->tscs_->upper_bound(pos);
    //printf("get_tsc pos;%lx tsc; %lx\n", pos, i->second.tsc);
    //if (i != tscs_.begin() && i != tscs_.end() && i->second.tsc != 0) {
    while (i != imp_->tscs_->begin()) {
        --i;
        tsc = i->second.tsc;
        if (tsc != 0) {
            tscs::const_iterator t(i);
            got_it = get_next_tsc(*imp_->tscs_, t, next_tsc);
            break;
        }
    }

    return got_it;
}

// Get wider tsc frame (a-1 ... b+1)   from [a-1, a, b, b+1] where scheduling should point to
bool tsc_heuristics::get_tsc_wide_range(ipt_pos pos, uint64_t& tsc, uint64_t& next_tsc)
{
    bool got_it = false;
    auto i = imp_->tscs_->upper_bound(pos);
    while (i != imp_->tscs_->begin()) {
        --i;
        tsc = i->second.tsc;
        //printf("[%lx..", tsc);
        if (tsc != 0) {
            tscs::const_iterator p(i);
            got_it = get_prev_tsc(*imp_->tscs_, p, tsc);
            //printf("%lx....", tsc);
            tscs::const_iterator t(i);
            got_it = get_next_tsc(*imp_->tscs_, t, next_tsc);
            //printf("%lx..", next_tsc);
            got_it = get_next_tsc(*imp_->tscs_, t, next_tsc);
            //printf("%lx]\n", next_tsc);
            break;
        }
    }

    return got_it;
}


#if 0
// Original, get one tsc slot where scheduling should point to
bool tsc_heuristics::get_tsc(ipt_pos pos, uint64_t& tsc, uint64_t& next_tsc)
{
    bool got_it = false;

    auto i = imp_->tscs_->upper_bound(pos);
    while (i != imp_->tscs_->begin()) {
        --i;
        tsc = i->second.tsc;
        if (tsc != 0) {
            tscs::const_iterator t(i);
            got_it = get_next_tsc(*imp_->tscs_, t, next_tsc);
            break;
        }
    }

    return got_it;
}
#endif

void tsc_heuristics::iterate_tsc_blocks(callback_func callback) const
{
/*
#task <task enum> "<process name>"  // multiprocessing block per process
  #enter <cpuid> <start_tsc>        // schedule in
  #block <cpuid> <start_tsc> <end_tsc> <start_offset> <end_offset> <last_psb_offset>
  // Problem with trace without timestamp, overflow or ???
  #block <cpuid> <start_tsc> <end_tsc> <start_offset> <end_offset> <last_psb_offset>
  #leave <cpuid> <end_tsc>          // schedule out
*/
    // TODO
    bool        have_block = false;
    ipt_pos     start_pos;
    ipt_pos     end_pos;
    ipt_offset  block_size;
    bool        have_tsc   = false;
    uint64_t    start_tsc;
    uint64_t    end_tsc;

    // iterate timing packets, coalescing them to two kinds of blocks:
    // ones that have or do not have timing information for each
    // IPT packet
    for (auto t = imp_->tscs_->begin(); t != imp_->tscs_->end(); ++t) {
        uint64_t tsc      = t->second.tsc;
        uint64_t next_tsc = 0;
        bool     got_tsc  = (tsc != 0);
        //printf("tscs; type %d; tsc %lx; have_tsc %d, got_tsc %d\n", t->second.type, tsc, have_tsc, got_tsc);

        if (tsc) {
            tscs::const_iterator i(t);
            get_next_tsc(*imp_->tscs_, i, next_tsc);
            if (!next_tsc) {
                // There is no end_tsc for that block, skip it
                got_tsc = false;
            }
        }

        if (!have_block) {
            // start the first block
            start_pos  = t->first;
            block_size = 0;
            start_tsc  = tsc;
            end_tsc    = next_tsc;
            have_tsc   = got_tsc;
            have_block = true;
        } else {
            end_pos    = t->first;
            block_size = t->first - start_pos;
            if (have_tsc == got_tsc &&
                start_tsc <= tsc && end_tsc <= next_tsc)
            {
                // the new timing packet can be coalesced
                end_tsc = next_tsc;
            } else {
                // output the previously coalesced block and start new
                callback(tsc_item_type::TSC, {start_pos, end_pos},
                         have_tsc, {start_tsc, end_tsc});
                start_pos  = t->first;
                block_size = 0;
                start_tsc  = tsc;
                end_tsc    = next_tsc;
                have_tsc   = got_tsc;
            }
        }
    }
    if (have_block && block_size != 0) {
        callback(tsc_item_type::TSC, {start_pos, end_pos}, have_tsc, {start_tsc, end_tsc});
    }
}

ipt_pos tsc_heuristics::get_last_psb(ipt_pos current_pos)
{
    auto i = imp_->strts_->upper_bound(current_pos);
    if (i != imp_->strts_->begin()) {
        --i;
    }
    //printf("get_last_startpoint: %lx (%d)\n", i->first, i->second);
    return i->first;
}

bool tsc_heuristics::get_next_valid_tsc(ipt_pos  current_pos,
                        ipt_pos& next_pos,
                        uint64_t& next_tsc)
{
    bool got_it = false;

    auto i = find_if(imp_->tscs_->upper_bound(current_pos),
                     imp_->tscs_->end(),
                     [](tscs::const_reference a) {
                         return a.second.tsc != 0;
                     });
    if (i != imp_->tscs_->end()) {
        next_pos = i->first;
        next_tsc = i->second.tsc;
        got_it   = true;
    }

    return got_it;
}

void tsc_heuristics::dump() const
{
    if (imp_->tscs_) {
        for (const auto& t : *imp_->tscs_) {
            dump_tsc(t);
        }
    }
}

void tsc_heuristics::dump_tscs() const
{
    if (imp_->tscs_) {
        for (const auto& t : *imp_->tscs_) {
            printf("tscs - p:%lx tsc:%lx\n", t.first, t.second.tsc);
        }
    }
}


} // sat
