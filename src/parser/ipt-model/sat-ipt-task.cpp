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
#include "sat-ipt-task.h"
#include "sat-getline.h"
#include "sat-log.h"
#include "sat-ipt.h"
#include "sat-ipt-block.h"
#include <sstream>
#include <vector>
#include <cinttypes>

namespace sat {

using namespace std;

struct ipt_task::imp {
    tid_t                         tid_;
    string                        name_;
    vector<shared_ptr<ipt_block>> blocks_;
    ipt_pos                       size_;
}; // ipt_task::imp


ipt_task::ipt_task(tid_t tid, const string& name) :
    imp_(make_unique<imp>())
{
    imp_->tid_  = tid;
    imp_->name_ = name;
}

ipt_task::~ipt_task()
{
}

tid_t ipt_task::tid() const
{
    return imp_->tid_;
}

const string& ipt_task::name() const
{
    return imp_->name_;
}

uint64_t ipt_task::size() const
{
    return imp_->size_;
}

bool ipt_task::get_earliest_tsc(uint64_t& tsc) const
{
    bool got_it = false;

    if (imp_->blocks_.size()) {
        tsc    = imp_->blocks_.front()->tsc_.first;
        if (tsc != 0)
            got_it = true;
    }

    return got_it;
}

void ipt_task::append_block(shared_ptr<ipt_block> block)
{
    imp_->blocks_.push_back(block);
    if (block->type_ == ipt_block::TRACE) {
        // only count processable TRACE
        imp_->size_ += block->pos_.second - block->pos_.first;
    }
}

void ipt_task::iterate_blocks(callback_func callback) const
{
    for (auto b : imp_->blocks_) {
        if (!callback(b)) {
            break;
        }
    }
}

bool ipt_task::serialize(ostream& stream) const
{
    unsigned ipt_block_count = 0;
    iterate_blocks([&](shared_ptr<ipt_block> b) {
        if (b->type_ == ipt_block::TRACE) {
            ++ipt_block_count;
        }
        return true;
    });
    stream << imp_->tid_ << " " << quote(imp_->name_) << endl
           << "  # " << ipt_block_count << " IPT blocks:" << endl;
    for (const auto& b : imp_->blocks_) {
        if (b->type_ == ipt_block::TRACE) {
            stream << "  block " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << " "        << b->tsc_.second
                   << " "        << b->pos_.first
                   << " "        << b->pos_.second
                   << " "        << b->psb_
                   << dec << endl;
        } else if (b->type_ == ipt_block::SCHEDULE_IN) {
            stream << "  enter " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << dec << endl;
        } else if (b->type_ == ipt_block::SCHEDULE_OUT) {
            stream << "  leave " << b->cpu_
                   << hex
                   << " "        << b->tsc_.second
                   << dec << endl;
        } else if (b->type_ == ipt_block::BAD) {
            stream << "  bad   " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << " "        << b->tsc_.second
                   << dec << endl;
        }
    }

    return true; // TODO
}


shared_ptr<ipt_task> ipt_task::deserialize(istream&       stream,
                                           string&        tag,
                                           istringstream& line)
{
    shared_ptr<ipt_task> result{nullptr};

    tag = "";

    tid_t tid;
    if (line >> dec >> tid) {
        string name;
        if (dequote(line, name)) {
            result = make_shared<ipt_task>(tid, name);
            while (get_tagged_line(stream, tag, line) &&
                   (tag == "block" ||
                    tag == "bad"   ||
                    tag == "enter" ||
                    tag == "leave"))
            {
                shared_ptr<ipt_block> block;
                unsigned              cpu;
                uint64_t              t1, t2;
                ipt_pos               psb=0;

                if (tag == "block") {
                    tag = "";
                    ipt_offset o1, o2;
                    if (line >> cpu >> hex >> t1 >> t2 >> o1 >> o2 >> psb >> dec) {
                        block = make_shared<ipt_block>(ipt_block{
                                                       ipt_block::TRACE,
                                                       {o1, o2},
                                                       {t1, t2},
                                                       true, tid, cpu, psb});
                    }
                } else if (tag == "enter") {
                    if (line >> cpu >> hex >> t1 >> dec) {
                        block = make_shared<ipt_block>(ipt_block{
                                                       ipt_block::SCHEDULE_IN,
                                                       {},
                                                       {t1, t1},
                                                       true, tid, cpu, psb});
                    }
                } else if (tag == "leave") {
                    if (line >> cpu >> hex >> t2 >> dec) {
                        block = make_shared<ipt_block>(ipt_block{
                                                       ipt_block::SCHEDULE_OUT,
                                                       {},
                                                       {t2, t2},
                                                       true, tid, cpu, psb});
                    }
                } else if (tag == "bad") {
                    if (line >> cpu >> hex >> t1 >> t2 >> dec) {
                        block = make_shared<ipt_block>(ipt_block{
                                                       ipt_block::BAD,
                                                       {},
                                                       {t1, t2},
                                                       true, tid, cpu, psb});
                    }
                }
                if (block) {
                    result->append_block(block);
                } else {
                    result = nullptr;
                    SAT_ERR("syntax error in collection: broken block '%s'\n",
                            tag.c_str());
                    break;
                }
            } // while
        } else {
            SAT_ERR("syntax error in collection: broken block '%s'\n",
                    tag.c_str());
        }
    } else {
        SAT_ERR("syntax error in collection: task ID\n");
    }

    return result;
}

void ipt_task::dump() const
{
    printf("TASK %u\n", imp_->tid_);
    for (const auto& b : imp_->blocks_) {
        printf("[%" PRIx64 " .. %" PRIx64 ") [%" PRIx64 " .. %" PRIx64 ")",
               b->pos_.first, b->pos_.second,
               b->tsc_.first, b->tsc_.second);
        if (b->has_tid_) {
            printf(" %u\n", b->tid_);
        } else {
            printf(" NO TID\n");
        }
    }
}

} // namespace sat
