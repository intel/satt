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

// TODO:
// - Add heuristics for this kind of case:
//     clock_new/cpu2.bin
//     ------------------
//     008cd2f8: 0abbcce1d72      STS 112, pass 1
//     ...
//     008d3573: 0abbcda0000      MTC 208, pass 5
//     008d3639: 00000000000     SKIP 232, pass 3
//     008d57a1: 00000000000      MTC 233, pass 0 <=
//     ... 322 MTCs and OVERFLOWs with zero TSC ...
//     008e9602: 00000000000      MTC  37, pass 0 <=
//     008e9647: 0abbd0ba000     SKIP  93, pass 4
//     008eb65c: 0abbd0bc000      MTC  94, pass 4
//     ...
//     00906edb: 0abbd4effbe      STS 119, pass 1
//
// - Zero is a valid tsc value;
//   use a signed type and value -1 to indicate a missing tsc.
//
#include "sat-rtit-tsc-heuristics.h"
#include "sat-rtit-workarounds.h"
#include "sat-file-input.h"
#include <map>
#include <vector>
#include <algorithm>
#include <cinttypes>

namespace sat {

using namespace std;

class tsc_item {
public:
    enum {
        BEGIN, STS, MTC, OVF, SKIP, PGE, END
    }        type;
    int      mtc; // for overflows (OVF) and skips (SKIP), mtc and tsc...
    uint64_t tsc; // ...have the end time of the condition
    //uint64_t next; // start tsc of the next time slot
    uint     passes;

    void set_pass(unsigned p) { passes |= (1 << p); }
    bool is_pass(unsigned p) const { return passes & (1 << p); }
};

// use a multimap, because synthetic MTCs share the same offset
typedef multimap<rtit_pos, tsc_item> tscs;


void calculate_mtc_values_for_stss(tscs& tscs, unsigned rng)
{
    for (auto t = tscs.begin(); t != tscs.end(); ++t) {
        if (t->second.type == tsc_item::STS) {
            // calculate the MTC value of the STS
            tsc_item& sts = t->second;
            sts.mtc = (sts.tsc >> (7 + 2 * rng)) & 0xff;
            sts.set_pass(1);
        }
    }
}

void spread_mtcs(tscs& tscs, unsigned rng)
{
    tsc_item* prev_mtc = 0;
    for (auto t = tscs.begin(); t != tscs.end(); ++t) {
        if (t->second.type == tsc_item::STS || t->second.type == tsc_item::MTC)
        {
            prev_mtc = &t->second;
        } else {
            if (t->second.type == tsc_item::OVF  ||
                t->second.type == tsc_item::SKIP ||
                t->second.type == tsc_item::PGE  ||
                t->second.type == tsc_item::BEGIN)
            {
                // find the first non-OVF
                const auto next_mtc = find_if(t, tscs.end(),
                                              [](tscs::reference a) {
                                                  return a.second.type ==
                                                             tsc_item::MTC ||
                                                         a.second.type ==
                                                             tsc_item::STS;
                                              });
                if (next_mtc != tscs.end()) {
                    // save pointer to BEGIN in case we need to spread
                    // mtc backwards over an initial SKIP;
                    // this is needed for circular buffer traces
                    tsc_item* begin = 0;
                    if (t->second.type == tsc_item::BEGIN) {
                        begin = &t->second;
                    }
                    auto last_ovf = next_mtc;
                    --last_ovf;
                    if (prev_mtc && (unsigned char)(next_mtc->second.mtc -
                                                    prev_mtc->mtc) <= 1)
                    {
                        for_each(t, next_mtc,
                                 [prev_mtc](tscs::reference a) {
                                     a.second.mtc = prev_mtc->mtc;
                                     a.second.set_pass(2);
                                 });
                    } else {
                        t = last_ovf;
                    }
                    last_ovf->second.mtc =
                        (unsigned char)
                            (next_mtc->second.mtc -
                             (next_mtc->second.type == tsc_item::MTC));
                    last_ovf->second.set_pass(3);
                    // also copy mtc from initial SKIP to BEGIN;
                    // this is needed for circular buffer traces
                    if (begin && last_ovf->second.type == tsc_item::SKIP) {
                        begin->mtc    = last_ovf->second.mtc;
                        begin->passes = last_ovf->second.passes;
                    }
                } else {
                    break;
                }
            }
            prev_mtc = 0;
        }
    }
}

void spread_tscs(tscs& tscs, unsigned rng, unsigned max_mtc_gap)
{
    int64_t  delta = 1 << (7 + 2 * rng);
    uint64_t mask = 0xffffffffffffffff << (7 + 2 * rng);

    auto is_sts_or_has_no_mtc = [](tscs::reference a) {
        return a.second.type == tsc_item::STS || a.second.mtc == -1;
    };

    for (auto t = tscs.begin(); t != tscs.end(); ++t) {
        if (t->second.type == tsc_item::STS) {
            {
                auto last_mtc = tscs::reverse_iterator(t);
                auto first_mtc = find_if(last_mtc,
                                         tscs.rend(),
                                         is_sts_or_has_no_mtc);
                tscs::const_pointer prev_mtc = &*t;
                for (;
                     last_mtc != tscs.rend() && last_mtc != first_mtc;
                     ++last_mtc)
                {
                    if (last_mtc->second.tsc == 0) {
                        int64_t sign =
                            (char)(unsigned char)(last_mtc->second.mtc -
                                                  prev_mtc->second.mtc);
                        if (abs(sign) <= max_mtc_gap) {
                            last_mtc->second.tsc  = prev_mtc->second.tsc;
                            last_mtc->second.tsc += sign * delta;
                            last_mtc->second.tsc &= mask;
                            last_mtc->second.set_pass(4);
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                    prev_mtc = &*last_mtc;
                }
            }
            {
                auto first_mtc = t;
                ++first_mtc;
                auto last_mtc = find_if(first_mtc,
                                        tscs.end(),
                                        is_sts_or_has_no_mtc);
                tscs::const_pointer prev_mtc = &*t;
                for (;
                     first_mtc != tscs.end() && first_mtc != last_mtc;
                     ++first_mtc)
                {
                    if (first_mtc->second.tsc == 0) {
                        int64_t sign =
                            (char)(unsigned char)(first_mtc->second.mtc -
                                                  prev_mtc->second.mtc);
                        if (sign == 0) {
                            first_mtc->second.tsc = prev_mtc->second.tsc;
                            first_mtc->second.set_pass(5);
                        } else if (abs(sign) <= max_mtc_gap) {
                            first_mtc->second.tsc  = prev_mtc->second.tsc;
                            first_mtc->second.tsc += sign * delta;
                            first_mtc->second.tsc &= mask;
                            first_mtc->second.set_pass(5);
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                    prev_mtc = &*first_mtc;
                }
            }
        }
    }
}

void jump_tscs_over_mtc_gaps(tscs& tscs, unsigned rng, unsigned max_mtc_gap)
{
    {
        const tsc_item* prev_mtc = 0;
        for (auto t = tscs.begin(); t != tscs.end(); ++t) {
            if (t->second.mtc != -1) {
                if (t->second.tsc == 0 && prev_mtc && prev_mtc->tsc != 0) {
                    unsigned gap = (unsigned char)(t->second.mtc -
                                                   prev_mtc->mtc);
                    if (gap <= max_mtc_gap) {
                        t->second.tsc  = prev_mtc->tsc;
                        t->second.tsc &= 0xffffffffffffffff << (7 + 2 * rng);
                        t->second.tsc += gap << (7 + 2 * rng);
                        t->second.set_pass(6);
                    }
                }
                prev_mtc = &t->second;
            }
        }
    }

    // TODO: this is ugly since it is mostly copy-pasted from above;
    //       abstract the common parts
    {
        const tsc_item* prev_mtc = 0;
        for (auto t = tscs.rbegin(); t != tscs.rend(); ++t) {
            if (t->second.mtc != -1) {
                if (t->second.tsc == 0 && prev_mtc && prev_mtc->tsc != 0) {
                    unsigned gap = (unsigned char)(prev_mtc->mtc -
                                                   t->second.mtc);
                    if (gap <= max_mtc_gap) {
                        t->second.tsc  = prev_mtc->tsc;
                        t->second.tsc &= 0xffffffffffffffff << (7 + 2 * rng);
                        t->second.tsc -= gap << (7 + 2 * rng);
                        t->second.set_pass(7);
                    }
                }
                prev_mtc = &t->second;
            }
        }
    }
}

bool get_next_tsc(const tscs&           tscs,
                  tscs::const_iterator& t,
                  unsigned              rng,
                  uint64_t&             tsc)
{
    bool got_it = false;

    uint64_t current_tsc = t->second.tsc;

    if (current_tsc != 0) {
        // first calculate the next MTC value
        int64_t  delta       = 1 << (7 + 2 * rng);
        uint64_t mask        = 0xffffffffffffffff << (7 + 2 * rng);
        tsc = (current_tsc + delta) & mask;
        got_it = true;

        // then see if the next tsc (e.g. an STS) is closer
        for (++t; t != tscs.end(); ++t)
        {
            if (t->second.tsc != 0 && t->second.tsc > current_tsc) {
                if (t->second.tsc < tsc) {
                    tsc = t->second.tsc;
                }
                break;
            }
        }
    }

    return got_it;
}


class tsc_heuristics_imp : public rtit_parser_output {
public:
    tsc_heuristics_imp()
    {
        tscs_.insert({{0, 0},      {tsc_item::BEGIN, -1, 0}});
    }

    void sts()
    {
        tscs_.insert({parsed_.pos, {tsc_item::STS, -1, parsed_.sts.tsc}});
    }

    void mtc()
    {
        // mtc is only 8 bits; safely static cast to suppress compiler warning
        tscs_.insert({parsed_.pos,
                     {tsc_item::MTC, static_cast<int>(parsed_.mtc.tsc), 0}});
        rng_ = parsed_.mtc.rng;
    }

    void fup_buffer_overflow()
    {
        tscs_.insert({parsed_.pos, {tsc_item::OVF, -1, 0}});
    }

    void fup_pge()
    {
        tscs_.insert({parsed_.pos, {tsc_item::PGE, -1, 0}});
    }

    void skip(rtit_pos pos, rtit_offset count)
    {
        tscs_.insert({pos, {tsc_item::SKIP, -1, 0}});
    }

    void apply()
    {
        tscs_.insert({{file_size_, 0}, {tsc_item::END,   -1, 0}});

        calculate_mtc_values_for_stss(tscs_, rng_);
        spread_mtcs(tscs_, rng_);
        //const int MAX_MTC_GAP = 82;
        const int MAX_MTC_GAP = 150;
        spread_tscs(tscs_, rng_, 1);
        jump_tscs_over_mtc_gaps(tscs_, rng_, MAX_MTC_GAP);
    }

    bool get_initial_tsc(rtit_pos& pos, uint64_t& initial_tsc) const
    {
        bool got_it = false;

        auto i = tscs_.begin();
        if (i != tscs_.end()                  &&
            i->second.type == tsc_item::BEGIN &&
            i->second.mtc  != -1              &&
            i->second.tsc  != 0)
        {
            pos         = i->first;
            initial_tsc = i->second.tsc;
            got_it = true;
        }

        return got_it;
    }

    bool get_tsc(rtit_pos pos, uint64_t& tsc)
    {
        bool got_it = false;

        auto i = tscs_.upper_bound(pos);
        if (i != tscs_.begin()) {
            --i;
            if (i->second.tsc != 0) {
                tsc = i->second.tsc;
                got_it = true;
            }
        }

        return got_it;
    }

    bool get_tsc(rtit_pos pos, uint64_t& tsc, uint64_t& next_tsc) const
    {
        bool got_it = false;

        auto i = tscs_.upper_bound(pos);
        //if (i != tscs_.begin() && i != tscs_.end() && i->second.tsc != 0) {
        if (i != tscs_.begin()) {
            --i;
            tsc = i->second.tsc;
            tscs::const_iterator t(i);
            got_it = get_next_tsc(tscs_, t, rng_, next_tsc);
        }

        return got_it;
    }

    bool get_next_valid_tsc(rtit_pos  current_pos,
                            rtit_pos& next_pos,
                            uint64_t& next_tsc)
    {
        bool got_it = false;

        auto i = find_if(tscs_.upper_bound(current_pos),
                         tscs_.end(),
                         [](tscs::const_reference a) {
                             return a.second.tsc != 0;
                         });
        if (i != tscs_.end()) {
            next_pos = i->first;
            next_tsc = i->second.tsc;
            got_it   = true;
        }

        return got_it;
    }

    void iterate(tsc_heuristics::callback_func callback) const
    {
        bool        have_block = false;
        rtit_pos    start_pos;
        rtit_pos    end_pos;
        rtit_offset block_size;
        bool        have_tsc   = false;
        uint64_t    start_tsc;
        uint64_t    end_tsc;

        // iterate timing packets, coalescing them to two kinds of blocks:
        // ones that have or do not have timing information for each
        // RTIT packet
        for (auto t = tscs_.begin(); t != tscs_.end(); ++t) {
            uint64_t tsc      = t->second.tsc;
            uint64_t next_tsc = 0;
            bool     got_tsc  = (tsc != 0);
            if (tsc) {
                tscs::const_iterator i(t);
                get_next_tsc(tscs_, i, rng_, next_tsc);
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
                block_size = t->first.offset_ - start_pos.offset_;
                if (have_tsc == got_tsc &&
                    start_tsc <= tsc && end_tsc <= next_tsc)
                {
                    // the new timing packet can be coalesced
                    end_tsc = next_tsc;
                } else {
                    // output the previously coalesced block and start new
                    callback({start_pos, end_pos},
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
            callback({start_pos, end_pos}, have_tsc, {start_tsc, end_tsc});
        }
    }


    static const char* type2str(unsigned type)
    {
        return type == tsc_item::BEGIN ? "BEGIN"    :
               type == tsc_item::STS   ? "STS"      :
               type == tsc_item::MTC   ? "MTC"      :
               type == tsc_item::OVF   ? "OVERFLOW" :
               type == tsc_item::SKIP  ? "SKIP"     :
               type == tsc_item::PGE   ? "PGE"      :
               type == tsc_item::END   ? "END"      :
                                         "UNKNOWN";
    }

    static void dump_passes(const tsc_item& i)
    {
        for (unsigned pass = 1; pass <= 7; ++pass) {
            if (i.is_pass(pass)) {
                printf(" %u", pass);
            } else {
                printf("  ");
            }
        }
    }

    static void dump_tsc(uint64_t tsc)
    {
        if (tsc) {
            printf("%011" PRIx64, tsc);
        } else {
            printf("???????????");
        }
    }

    static void dump_offsets(vector<uint64_t> offsets)
    {
        unsigned       column  = 0;
        const unsigned columns = 7;

        for (uint64_t offset : offsets) {
            if (column == columns) {
                printf("\n");
                column = 0;
            }
            printf("%s%08" PRIx64, column ? " " : "", offset);
            ++column;
        }

        if (column) {
            printf("\n");
        }
    }

    void dump() const
    {
        printf("CONTINUOUS TSC BLOCKS:\n");
        iterate([&](pair<rtit_pos, rtit_pos> pos,
                    bool                     has_tsc,
                    pair<uint64_t, uint64_t> tsc)
        {
            printf("[%08" PRIx64 " .. %08" PRIx64 "): ",
                   pos.first.offset_, pos.second.offset_);
            if (has_tsc) {
                dump_tsc(tsc.first);
                printf("\n");
            } else {
                printf("NO TSC\n");
            }
        });
        printf("---\n");

        uint64_t         max_jump;
        vector<uint64_t> long_jumps;
        vector<uint64_t> bad_mtcs;
        vector<uint64_t> time_travels;

        collect_tsc_continuity_problems(max_jump,
                                        long_jumps,
                                        bad_mtcs,
                                        time_travels);

        if (time_travels.size()) {
            printf("THERE ARE %lu JUMPS BACK IN TIME:\n",
                   time_travels.size());
            dump_offsets(time_travels);
            printf("---\n");
        }
        if (long_jumps.size()) {
            printf("THERE ARE %lu TSC JUMPS LONGER THAN %" PRIx64 ":\n",
                   long_jumps.size(), max_jump);
            dump_offsets(long_jumps);
            printf("---\n");
        }
        if (bad_mtcs.size()) {
            printf("THERE ARE %lu MTCS WITOUT TSCS:\n", bad_mtcs.size());
            dump_offsets(bad_mtcs);
            printf("---\n");
        }

        printf(
"RTIT                                       TSC                HEURISTICS\n"
"OFFSET    [TSC RANGE               )       TICKS     TYPE MTC PASSES\n"
"---------------------------------------------------------------------------\n");
        for (auto t = tscs_.begin(); t != tscs_.end(); ++t) {
            uint64_t tsc = t->second.tsc;
            uint64_t next_tsc;
            tscs::const_iterator i(t);
            if (!get_next_tsc(tscs_, i, rng_, next_tsc)) {
                next_tsc = 0;
            }
            printf("%08" PRIx64 ": [", t->first.offset_);
            dump_tsc(tsc);
            printf("..");
            dump_tsc(next_tsc);
            if (tsc && next_tsc) {
                printf(") %11" PRIx64, next_tsc - tsc);
            } else {
                printf(")            ");
            }
            printf(" %8s %3d", type2str(t->second.type), t->second.mtc);
            dump_passes(t->second);
            printf("\n");
        }
    }

    void collect_tsc_continuity_problems(uint64_t&         max_jump,
                                         vector<uint64_t>& long_jumps,
                                         vector<uint64_t>& bad_mtcs,
                                         vector<uint64_t>& time_travels) const
    {
        max_jump = 1 << (7 + 2 * rng_);
        tscs::const_pointer prev = 0;
        for (auto& i : tscs_) {
            if (prev) {
                if (i.second.mtc != -1) {
                    if (i.second.tsc < prev->second.tsc) {
                        if (i.second.tsc != 0 &&
                            i.second.tsc != tsc_item::OVF &&
                            i.second.tsc != tsc_item::SKIP)
                        {
                            time_travels.push_back(i.first.offset_);
                        }
                    } else if (prev->second.mtc != -1 &&
                               prev->second.tsc + max_jump < i.second.tsc)
                    {
                        long_jumps.push_back(i.first.offset_);
                    }
                }
            }
            if (i.second.type == tsc_item::MTC && i.second.tsc == 0) {
                bad_mtcs.push_back(i.first.offset_);
            }
            prev = &i;
        }
    }

    long     file_size_;

private:
    tscs     tscs_;
    unsigned rng_;
}; // class tsc_heuristics_imp


tsc_heuristics::tsc_heuristics() :
    imp_{new tsc_heuristics_imp}
{
}

tsc_heuristics::~tsc_heuristics()
{
}

bool tsc_heuristics::parse_rtit(const std::string& path)
{
    bool parsed = false;

    typedef file_input<rtit_parser_input> input_type;
    typedef rtit_parser<
                postpone_early_mtc<
                synthesize_dropped_mtcs<
            >>> parser_type;

    shared_ptr<input_type> input{new input_type};
    if (input->open(path)) {
        parser_type parser(input, imp_);;
        parsed = parser.parse(false, true);
        imp_->file_size_ = input->tell();
    }

    return parsed;
}

void tsc_heuristics::apply()
{
    imp_->apply();
}

bool tsc_heuristics::get_initial_tsc(rtit_pos& pos, uint64_t& initial_tsc) const
{
    return imp_->get_initial_tsc(pos, initial_tsc);
}

bool tsc_heuristics::get_tsc(rtit_pos pos, uint64_t& tsc) const
{
    return imp_->get_tsc(pos, tsc);
}

bool tsc_heuristics::get_tsc(rtit_pos  pos,
                             uint64_t& tsc,
                             uint64_t& next_tsc) const
{
    return imp_->get_tsc(pos, tsc, next_tsc);
}

bool tsc_heuristics::get_next_valid_tsc(rtit_pos  current_pos,
                                        rtit_pos& next_pos,
                                        uint64_t& next_tsc)
{
    return imp_->get_next_valid_tsc(current_pos, next_pos, next_tsc);
}

void tsc_heuristics::iterate(callback_func callback) const
{
    imp_->iterate(callback);
}

void tsc_heuristics::dump() const
{
    imp_->dump();
}

} // namespace sat
