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
#include "sat-ipt-file.h"
#include "sat-ipt-block.h"
#include "sat-ipt-tsc-heuristics.h"
#include "sat-ipt-scheduling-heuristics.h"
#include "sat-sideband-model.h"
#include <vector>
#include <iostream>
#include <cinttypes>

namespace sat {

using block_set = vector<shared_ptr<ipt_block>>;

struct ipt_file::imp {
    unsigned     cpu_;
    const string path_;
    block_set    blocks_;
    block_set::iterator iterator_;
};

ipt_file::ipt_file(unsigned                         cpu,
                   const string&                    path,
                   shared_ptr<const sideband_model> sideband,
                   const string&                    sideband_path) :
    imp_(new imp{cpu, path, block_set{}})
{
    // apply tsc heuristics
    //auto schedulings = make_shared<scheduling_heuristics>(cpu, ipt_path, sideband);
    tsc_heuristics tscs(sideband_path);
    tscs.parse_ipt(imp_->path_);
    tscs.apply();
    // walk through tsc data
    tscs.iterate_tsc_blocks([&](tsc_item_type            type,
                                pair<ipt_pos, ipt_pos>   pos,
                                bool                     has_tsc,
                                pair<uint64_t, uint64_t> tsc)
    {
        // TODO
        //std::cout << "BLOCKS " << pos.first << "--" << pos.second << " type: " << type << " have_tsc: " << has_tsc << endl;
        if (has_tsc) {
            shared_ptr<ipt_block> block(new ipt_block{
                ipt_block::TRACE, pos, tsc, false, tid_t(), cpu, 0
            });
            //std::cout << "  BLOCKS TSC " << pos.first << "--" << pos.second << endl;
            imp_->blocks_.push_back(block);
        } 
    });

    //
    // now blocks_ has a list of all blocks that have timestamps
    //

    // apply scheduling heuristics
    auto schedulings = make_shared<scheduling_heuristics>(cpu, path, sideband, sideband_path);
    schedulings->apply();

    //printf("CPU %u\n", cpu);

    auto block = imp_->blocks_.begin();

    // fast forward tsc blocks until we get one that is at least partially
    // after the beginning of the first quantum
    uint64_t quantum_tsc = 0;
    bool     quantum_has_pos = false;
    size_t   quantum_pos = 0;
    tid_t    quantum_tid = 0;

    if (!schedulings->get_first_quantum_start(quantum_tsc,
                                         quantum_has_pos,
                                         quantum_pos,
                                         quantum_tid))
    {
        fprintf(stderr, "ERROR: No schedule first quantum detected!\n");
        exit(EXIT_FAILURE);
    }

    uint32_t erased_block_count=0;
    while (block != imp_->blocks_.end() &&
        quantum_tsc >= (*block)->tsc_.second)
    {
        // throw away the block that is fully before the first quantum
        erased_block_count++;
        block = imp_->blocks_.erase(block);
    }
    // Debug
    printf("# Erased block count=%d\n", erased_block_count);

    if (block != imp_->blocks_.end() &&
        quantum_tsc > (*block)->tsc_.first)
    {
        // the first tsc block needs to be split to remove the part
        // before the beginning of the first quantum;

        (*block)->tsc_.first = quantum_tsc;
        (*block)->pos_.first = quantum_pos;
        (*block)->has_tid_ = true;
        (*block)->tid_ = quantum_tid;
        (*block)->psb_ = tscs.get_last_psb(quantum_pos);

        // first find the splitting offset
        // then define a new block
/*        shared_ptr<ipt_block> tail(new ipt_block{
            ipt_block::TRACE,
            {quantum_pos, (*block)->pos_.second},
            {quantum_tsc, (*block)->tsc_.second},
            (*block)->has_tid_,
            (*block)->tid_,
            (*block)->cpu_,
            (*block)->psb_
        });
        // then remove the old block
        block = imp_->blocks_.erase(block);
        // finally, insert the new block and move to it
        block = imp_->blocks_.insert(block, tail);
*/
    }

    // get the starting tsc of the first tsc block
    if (block == imp_->blocks_.end()) {
        fprintf(stderr, "ERROR: NO USABLE TSC BLOCKS FOR CPU %u\n", cpu);
        //SAT_WARN("NO USABLE TSC BLOCKS FOR CPU %u\n", cpu);
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
                                      pair<ipt_pos, ipt_pos> pos)
    {
#if 0
        printf("  QUANTUM" \
               " [%" PRIx64 " .. %" PRIx64 ")" \
               " [%" PRIx64 " .. %" PRIx64 ") %u",
               tsc.first, tsc.second,
               has_pos.first  ? pos.first : 0,
               has_pos.second ? pos.second : 0,
               tid);
        printf("\n");
#endif
        uint64_t last_psb_tsc, b;
        ipt_pos last_psb_pos = tscs.get_last_psb(pos.first);
        tscs.get_tsc(last_psb_pos, last_psb_tsc, b);
        shared_ptr<ipt_block> schedule_in(new ipt_block{
            ipt_block::SCHEDULE_IN,
            {pos.first, pos.first}, // may or may not have valid values
            {last_psb_tsc, last_psb_tsc},
            true,
            tid,
            cpu,
            last_psb_pos
        });
        //printf("# schedule_in [%lx..%lx] --> %d\n", pos.first, pos.second, tid);
        block = imp_->blocks_.insert(block, schedule_in);
        ++block;


        while (block != imp_->blocks_.end()) {
            if (has_pos.second) {
                //printf("#    has_pos [%lx..%lx]\n", (*block)->pos_.first, (*block)->pos_.second);
                // fast-forward tsc blocks to the end of the quantum
                while (block != imp_->blocks_.end() &&
                       pos.second >= (*block)->pos_.second)
                {
                    //printf("#    block %lx..%lx\n", (*block)->pos_.first, (*block)->pos_.second);
                    // the whole tsc block is before the end of the quantum;
                    // mark it as belonging to the quantum
                    (*block)->tid_     = tid;
                    (*block)->has_tid_ = true;
                    (*block)->psb_ = tscs.get_last_psb((*block)->pos_.first);
                    ++block;
                }
                if (block != imp_->blocks_.end()) {
                    // we have found a tsc block whose end is after quantum end
                    if ((*block)->pos_.first < pos.second) {
                        //printf("#    head %lx..%lx | tail %lx..%lx\n", (*block)->pos_.first, pos.first, pos.second, (*block)->pos_.second);
                        // tsc block starts before end of quantum;
                        // split the tsc block
                        shared_ptr<ipt_block> tail(new ipt_block{
                            ipt_block::TRACE,
                            {pos.second, (*block)->pos_.second},
                            {tsc.second, (*block)->tsc_.second},
                            (*block)->has_tid_,
                            (*block)->tid_,
                            (*block)->cpu_,
                            tscs.get_last_psb(pos.second)
                        });
                        // truncate the original block
                        (*block)->pos_.second = pos.second;
                        (*block)->tsc_.second = tsc.second;
                        (*block)->tid_        = tid;
                        (*block)->has_tid_    = true;
                        (*block)->psb_ = tscs.get_last_psb((*block)->pos_.first);
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
                    (*block)->psb_ = tscs.get_last_psb((*block)->pos_.first);
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
                        ipt_pos p = (*block)->pos_.first;
                        ipt_pos block_1_end   = p;
                        ipt_pos block_2_begin = p;
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
                            shared_ptr<ipt_block> tail(new ipt_block{
                                ipt_block::TRACE,
                                {block_2_begin, (*block)->pos_.second},
                                {t, (*block)->tsc_.second},
                                (*block)->has_tid_,
                                (*block)->tid_,
                                (*block)->cpu_,
                                tscs.get_last_psb(block_2_begin)
                            });
                            // then truncate the first block
                            (*block)->pos_.second = block_1_end;
                            (*block)->tsc_.second = t_prev;
                            (*block)->tid_        = tid;
                            (*block)->has_tid_    = true;
                            (*block)->psb_ = tscs.get_last_psb((*block)->pos_.first);
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
                                (*block)->psb_ = tscs.get_last_psb((*block)->pos_.first);
                                // move onto next quantum
                                break;
                            } else {
                                // it is the second block;
                                // first define the new block
                                shared_ptr<ipt_block> tail(new ipt_block{
                                    ipt_block::TRACE,
                                    {block_2_begin, (*block)->pos_.second},
                                    {t, (*block)->tsc_.second},
                                    (*block)->has_tid_,
                                    (*block)->tid_,
                                    (*block)->cpu_,
                                    tscs.get_last_psb(block_2_begin)
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
            shared_ptr<ipt_block> schedule_out(new ipt_block{
                ipt_block::SCHEDULE_OUT,
                {pos.second, pos.second}, // may or may not have valid values
                {tsc.second, tsc.second},
                true,
                tid,
                cpu,
                pos.second
            });
            //printf("# schedule_out %lx\n\n", pos.second);
            block = imp_->blocks_.insert(block, schedule_out);
            ++block;
        }
    }); // iterate quantums

    //
    // all the IPT blocks now have tids, timestamps and ipt offsets;
    // SCHEDULE_IN and SCHEDULE_OUT blocks have timestamps
    //

    // take another pass through the blocks, inserting bad blocks where needed
#if 0
    auto prev = block = imp_->blocks_.begin();
    if (block != imp_->blocks_.end()) {
        ++block;
        while (block != imp_->blocks_.end()) {
            if ((*block)->tsc_.first != (*prev)->tsc_.second) {
                // there seems to be a time gap; insert a BAD block
                shared_ptr<ipt_block> bad(new ipt_block{
                    ipt_block::BAD,
                    {(*prev)->pos_.second, (*block)->pos_.first}, // valid or not
                    {(*prev)->tsc_.second, (*block)->tsc_.first},
                    (*prev)->has_tid_,
                    (*prev)->tid_,
                    (*prev)->cpu_,
                    0 // PSB pos 0 right? TODO check
                });
                block = imp_->blocks_.insert(block, bad);
                ++block;
            }
            prev = block++;
        }
    }
#endif

}

shared_ptr<ipt_block> ipt_file::begin() const
{
    shared_ptr<ipt_block> result = NULL;

    if ((imp_->iterator_ = imp_->blocks_.begin()) != imp_->blocks_.end()) {
        result = *(imp_->iterator_);
    }

    return result;
}

shared_ptr<ipt_block> ipt_file::current() const
{
    shared_ptr<ipt_block> result;

    if (imp_->iterator_ != imp_->blocks_.end()) {
        result = *(imp_->iterator_);
    }

    return result;
}

void ipt_file::advance() const
{
    ++imp_->iterator_;
}


ipt_file::~ipt_file()
{
}

} // sat
