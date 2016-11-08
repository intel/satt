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
#include "sat-rtit-file.h"
#include "sat-memory.h"
#include "sat-rtit-tsc-heuristics.h"
#include "sat-scheduling-heuristics.h"
#include "sat-rtit-block.h"
#include "sat-log.h"
#include <vector>
#include <cstdio>
#include <cinttypes>
#include <algorithm>

namespace sat {

using namespace std;

using block_set = vector<shared_ptr<rtit_block>>;

struct rtit_file::imp {
    unsigned            cpu_;
    const string        path_;
    block_set           blocks_;
    block_set::iterator iterator_;
};

rtit_file::rtit_file(unsigned                         cpu,
                     const string&                    path,
                     shared_ptr<const sideband_model> sideband,
                     const vm_sec_list_type& vm_section_list) :
    imp_(new imp{cpu, path, block_set{}})
{
    // apply tsc heuristics to rtit
    tsc_heuristics tscs;
    tscs.parse_rtit(imp_->path_);
    tscs.apply();

    // walk through the tsc data, creating blocks for continuous parts
    tscs.iterate([&](pair<rtit_pos, rtit_pos> pos,
                     bool                     has_tsc,
                     pair<uint64_t, uint64_t> tsc)
    {
        if (has_tsc) {
            shared_ptr<rtit_block> block(new rtit_block{
                rtit_block::RTIT, pos, tsc, false, tid_t(), cpu
            });
            imp_->blocks_.push_back(block);
        }
    });

    //
    // now blocks_ has a list of all blocks that have timestamps
    //

    // apply scheduling heuristics
    auto schedulings = make_shared<scheduling_heuristics>(cpu, path, sideband, vm_section_list);
    schedulings->apply();

    //printf("CPU %u\n", cpu);

    auto block = imp_->blocks_.begin();

    // fast forward tsc blocks until we get one that is at least partially
    // after the beginning of the first quantum
    uint64_t first_quantum_tsc;
    bool     first_quantum_has_pos;
    rtit_pos first_quantum_pos;
    schedulings->get_first_quantum_start(first_quantum_tsc,
                                         first_quantum_has_pos,
                                         first_quantum_pos);

    while (block != imp_->blocks_.end() &&
           first_quantum_tsc >= (*block)->tsc_.second)
    {
        // throw away the block that is fully before the first quantum
        block = imp_->blocks_.erase(block);
    }

    if (block != imp_->blocks_.end() &&
        first_quantum_tsc > (*block)->tsc_.first)
    {
        // the first tsc block needs to be split to remove the part
        // before the beginning of the first quantum;
        // first find the splitting offset
        rtit_pos pos;
        uint64_t tsc;
        if (first_quantum_has_pos) {
            pos = first_quantum_pos;
            tsc = first_quantum_tsc;
        } else {
            pos = (*block)->pos_.first;
            tsc = (*block)->tsc_.first;
            while (tsc < first_quantum_tsc &&
                   tscs.get_next_valid_tsc(pos, pos, tsc) )
            {
                // empty loop
            }
        }
        // then define a new block
        shared_ptr<rtit_block> tail(new rtit_block{
            rtit_block::RTIT,
            {pos, (*block)->pos_.second},
            {tsc, (*block)->tsc_.second},
            (*block)->has_tid_,
            (*block)->tid_,
            (*block)->cpu_
        });
        // then remove the old block
        block = imp_->blocks_.erase(block);
        // finally, insert the new block and move to it
        block = imp_->blocks_.insert(block, tail);
    }

    // get the starting tsc of the first tsc block
    if (block == imp_->blocks_.end()) {
        SAT_WARN("NO USABLE TSC BLOCKS FOR CPU %u\n", cpu);
#if 0 // we still might have schedule in/out blocks, so do not return
        return;
#endif
    }

    //
    // the list of blocks now starts at the first scheduling quantum
    //

    uint64_t first_tsc = (*block)->tsc_.first;

    // walk through scheduling quantums, inserting tids into the block set
    schedulings->iterate_quantums(first_tsc,
                                  [&](pair<uint64_t, uint64_t> tsc,
                                      tid_t                    tid,
                                      pair<bool, bool>         has_pos,
                                      pair<rtit_pos, rtit_pos> pos)
    {
#if 0
        printf("  QUANTUM" \
               " [%" PRIx64 " .. %" PRIx64 ")" \
               " [%" PRIx64 " .. %" PRIx64 ") %u",
               tsc.first, tsc.second,
               has_pos.first  ? pos.first.offset_ : 0,
               has_pos.second ? pos.second.offset_ : 0,
               tid);
        printf("\n");
#endif
        shared_ptr<rtit_block> schedule_in(new rtit_block{
            rtit_block::SCHEDULE_IN,
            {pos.first, pos.first}, // may or may not have valid values
            {tsc.first, tsc.first},
            true,
            tid,
            cpu
        });
        block = imp_->blocks_.insert(block, schedule_in);
        ++block;

        while (block != imp_->blocks_.end()) {
            if (has_pos.second) {
                // fast-forward tsc blocks to the end of the quantum
                while (block != imp_->blocks_.end() &&
                       pos.second >= (*block)->pos_.second)
                {
                    // the whole tsc block is before the end of the quantum;
                    // mark it as belonging to the quantum
                    (*block)->tid_     = tid;
                    (*block)->has_tid_ = true;
                    ++block;
                }
                if (block != imp_->blocks_.end()) {
                    // we have found a tsc block whose end is after quantum end
                    if ((*block)->pos_.first.offset_ < pos.second.offset_) {
                        // tsc block starts before end of quantum;
                        // split the tsc block
                        shared_ptr<rtit_block> tail(new rtit_block{
                            rtit_block::RTIT,
                            {pos.second, (*block)->pos_.second},
                            {tsc.second, (*block)->tsc_.second},
                            (*block)->has_tid_,
                            (*block)->tid_,
                            (*block)->cpu_
                        });
                        // truncate the original block
                        (*block)->pos_.second = pos.second;
                        (*block)->tsc_.second = tsc.second;
                        (*block)->tid_        = tid;
                        (*block)->has_tid_    = true;
                        // insert the new block, moving to it
                        ++block;
                        block = imp_->blocks_.insert(block, tail);
                        // deal with the new block in the next while iteration
                    } else {
                        // the whole tsc block is after the quantum;
                        // move onto the next quantum
                        break;
                    }
                }
            } else {
                // fast-forward blocks to the scheduling point tsc
                while (block != imp_->blocks_.end() &&
                       tsc.second >= (*block)->tsc_.second)
                {
                    // the whole tsc block is before the end of the quatum;
                    // mark it as belonging to the quantum
                    (*block)->tid_     = tid;
                    (*block)->has_tid_ = true;
                    ++block;
                }
                if (block != imp_->blocks_.end()) {
                    // we have found a tsc block whose end is after quantum end
                    if ((*block)->tsc_.first < tsc.second) {
                        // tsc block starts before end of quantum;
                        // split the tsc block so that every tsc range
                        // before the quantum end goes into the first block,
                        // and every tsc range after the quantum end goes
                        // to the second block
                        rtit_pos p = (*block)->pos_.first;
                        rtit_pos block_1_end   = p;
                        rtit_pos block_2_begin = p;
                        uint64_t t, t_prev = (*block)->tsc_.first;
                        while (p < (*block)->pos_.second &&
                               tscs.get_next_valid_tsc(p, p, t))
                        {
                            if (t <= tsc.second) {
                                block_1_end = p;
                                t_prev      = t;
                            }
                            if (t >= tsc.second) {
                                block_2_begin = p;
                                break;
                            }
                        }
                        if (block_1_end > (*block)->pos_.first  &&
                            block_1_end < (*block)->pos_.second &&
                            block_2_begin >= block_1_end             &&
                            block_2_begin < (*block)->pos_.second)
                        {
                            // we have two separate blocks; split;
                            // first define the second block
                            shared_ptr<rtit_block> tail(new rtit_block{
                                rtit_block::RTIT,
                                {block_2_begin, (*block)->pos_.second},
                                {t, (*block)->tsc_.second},
                                (*block)->has_tid_,
                                (*block)->tid_,
                                (*block)->cpu_
                            });
                            // then truncate the first block
                            (*block)->pos_.second = block_1_end;
                            (*block)->tsc_.second = t_prev;
                            (*block)->tid_        = tid;
                            (*block)->has_tid_    = true;
                            // finally, insert the second block and move to it
                            ++block;
                            block = imp_->blocks_.insert(block, tail);
                        } else {
                            // we have just one block; no need to split
                            if (block_1_end > (*block)->pos_.first &&
                                block_1_end < (*block)->pos_.second)
                            {
                                // it is the first block; truncate it
                                (*block)->pos_.second = block_1_end;
                                (*block)->tid_        = tid;
                                (*block)->has_tid_    = true;
                                // move onto next quantum
                                break;
                            } else {
                                // it is the second block;
                                // first define the new block
                                shared_ptr<rtit_block> tail(new rtit_block{
                                    rtit_block::RTIT,
                                    {block_2_begin, (*block)->pos_.second},
                                    {t, (*block)->tsc_.second},
                                    (*block)->has_tid_,
                                    (*block)->tid_,
                                    (*block)->cpu_
                                });
                                // then remove the old block
                                block = imp_->blocks_.erase(block);
                                // finally, insert the new block and move to it
                                block = imp_->blocks_.insert(block, tail);
                            }
                        }
                    } else {
                        // the whole tsc block is after the quantum;
                        // move onto the next quantum
                        break;
                    }
                }
            }
        }
        // only insert SCHEDULE_OUT if we know the precise time
        if (tsc.second != numeric_limits<uint64_t>::max()) {
            shared_ptr<rtit_block> schedule_out(new rtit_block{
                rtit_block::SCHEDULE_OUT,
                {pos.second, pos.second}, // may or may not have valid values
                {tsc.second, tsc.second},
                true,
                tid,
                cpu
            });
            block = imp_->blocks_.insert(block, schedule_out);
            ++block;
        }
    }); // iterate quantums

    //
    // all the RTIT blocks now have tids, timestamps and rtit offsets;
    // SCHEDULE_IN and SCHEDULE_OUT blocks have timestamps
    //

    // take another pass through the blocks, inserting bad blocks where needed
    auto prev = block = imp_->blocks_.begin();
    if (block != imp_->blocks_.end()) {
        ++block;
        while (block != imp_->blocks_.end()) {
            if ((*block)->tsc_.first != (*prev)->tsc_.second) {
                // there seems to be a time gap; insert a BAD block
                shared_ptr<rtit_block> bad(new rtit_block{
                    rtit_block::BAD,
                    {(*prev)->pos_.second, (*block)->pos_.first}, // valid or not
                    {(*prev)->tsc_.second, (*block)->tsc_.first},
                    (*prev)->has_tid_,
                    (*prev)->tid_,
                    (*prev)->cpu_
                });
                block = imp_->blocks_.insert(block, bad);
                ++block;
            }
            prev = block++;
        }
    }
}

rtit_file::~rtit_file()
{
}

string rtit_file::path() const
{
    return imp_->path_;
}

void rtit_file::iterate_blocks(callback_func callback) const
{
    for (const auto& b : imp_->blocks_) {
        callback(b);
    }
}

shared_ptr<rtit_block> rtit_file::begin() const
{
    shared_ptr<rtit_block> result;

    if ((imp_->iterator_ = imp_->blocks_.begin()) != imp_->blocks_.end()) {
        result = *(imp_->iterator_);
    }

    return result;
}

shared_ptr<rtit_block> rtit_file::current() const
{
    shared_ptr<rtit_block> result;

    if (imp_->iterator_ != imp_->blocks_.end()) {
        result = *(imp_->iterator_);
    }

    return result;
}

void rtit_file::advance() const
{
    ++imp_->iterator_;
}

void rtit_file::dump()
{
    printf("CPU %u:\n", imp_->cpu_);
    for (const auto& b : imp_->blocks_) {
        printf("[%" PRIx64 " .. %" PRIx64 ") [%" PRIx64 " .. %" PRIx64 ")",
               b->pos_.first.offset_, b->pos_.second.offset_,
               b->tsc_.first, b->tsc_.second);
        if (b->has_tid_) {
            printf(" %u\n", b->tid_);
        } else {
            printf(" NO TID\n");
        }
    }
}

} // namespace sat
