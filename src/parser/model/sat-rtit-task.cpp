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
#include "sat-rtit-task.h"
#include "sat-memory.h"
#include "sat-rtit-block.h"
#include "sat-getline.h"
#include "sat-log.h"
#include <vector>
#include <cinttypes>
#include <sstream>

namespace sat {

struct rtit_task::imp {
    explicit imp(tid_t tid, const string& name) :
        tid_(tid), name_(name), size_(), vmm_host_(false)
    {
    }

    tid_t                          tid_;
    string                         name_;
    vector<shared_ptr<rtit_block>> blocks_;
    rtit_offset                    size_;
    bool                           vmm_host_;
}; // rtit_task::imp

rtit_task::rtit_task(tid_t tid, const string& name) :
    imp_(make_unique<imp>(tid, name))
{
}

rtit_task::~rtit_task()
{
}

tid_t rtit_task::tid() const
{
    return imp_->tid_;
}

const string& rtit_task::name() const
{
    return imp_->name_;
}

uint64_t rtit_task::size() const
{
    return imp_->size_;
}

bool rtit_task::get_earliest_tsc(uint64_t& tsc) const
{
    bool got_it = false;

    if (imp_->blocks_.size()) {
        tsc    = imp_->blocks_.front()->tsc_.first;
        got_it = true;
    }

    return got_it;
}

void rtit_task::set_vmm_host()
{
    imp_->vmm_host_ = true;
}

bool rtit_task::is_vmm_host()
{
    return imp_->vmm_host_;
}

void rtit_task::append_block(shared_ptr<rtit_block> block)
{
    imp_->blocks_.push_back(block);
    if (block->type_ == rtit_block::RTIT) {
        // only count processable RTIT
        imp_->size_ += block->pos_.second.offset_ - block->pos_.first.offset_;
    }
}

void rtit_task::iterate_blocks(callback_func callback) const
{
    for (auto b : imp_->blocks_) {
        if (!callback(b)) {
            break;
        }
    }
}

bool rtit_task::serialize(ostream& stream) const
{
    unsigned rtit_block_count = 0;
    iterate_blocks([&](shared_ptr<rtit_block> b) {
        if (b->type_ == rtit_block::RTIT) {
            ++rtit_block_count;
        }
        return true;
    });
    stream << imp_->tid_ << " " << quote(imp_->name_) << endl
           << "  # " << rtit_block_count << " RTIT blocks:" << endl;
    for (const auto& b : imp_->blocks_) {
        if (b->type_ == rtit_block::RTIT) {
            stream << "  block " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << " "        << b->tsc_.second
                   << " "        << b->pos_.first.offset_
                   << " "        << b->pos_.first.lip_
                   << " "        << b->pos_.second.offset_
                   << " "        << b->pos_.second.lip_
                   << dec << endl;
        } else if (b->type_ == rtit_block::SCHEDULE_IN) {
            stream << "  enter " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << dec << endl;
        } else if (b->type_ == rtit_block::SCHEDULE_OUT) {
            stream << "  leave " << b->cpu_
                   << hex
                   << " "        << b->tsc_.second
                   << dec << endl;
        } else if (b->type_ == rtit_block::BAD) {
            stream << "  bad   " << b->cpu_
                   << hex
                   << " "        << b->tsc_.first
                   << " "        << b->tsc_.second
                   << dec << endl;
        }
    }

    return true; // TODO
}

shared_ptr<rtit_task> rtit_task::deserialize(istream&       stream,
                                             string&        tag,
                                             istringstream& line)
{
    shared_ptr<rtit_task> result{nullptr};

    tag = "";

    tid_t tid;
    if (line >> dec >> tid) {
        string name;
        if (dequote(line, name)) {
            result = make_shared<rtit_task>(tid, name);
            while (get_tagged_line(stream, tag, line) &&
                   (tag == "block" ||
                    tag == "bad"  ||
                    tag == "enter"    ||
                    tag == "leave"))
            {
                shared_ptr<rtit_block> block;
                unsigned               cpu;
                uint64_t               t1, t2;

                if (tag == "block") {
                    tag = "";
                    rtit_offset o1, o2;
                    rva         l1, l2;
                    if (line >> cpu
                             >> hex >> t1 >> t2 >> o1 >> l1 >> o2 >> l2 >> dec)
                    {
                        block = make_shared<rtit_block>(rtit_block{
                                                        rtit_block::RTIT,
                                                        {{o1, l1}, {o2, l2}},
                                                        {t1, t2},
                                                        true, tid, cpu});
                    } else {
                    }
                } else if (tag == "enter") {
                    if (line >> cpu >> hex >> t1 >> dec) {
                        block = make_shared<rtit_block>(rtit_block{
                                                        rtit_block::SCHEDULE_IN,
                                                        {},
                                                        {t1, t1},
                                                        true, tid, cpu});
                    }
                } else if (tag == "leave") {
                    if (line >> cpu >> hex >> t2 >> dec) {
                        block = make_shared<rtit_block>(rtit_block{
                                                        rtit_block::SCHEDULE_OUT,
                                                        {},
                                                        {t2, t2},
                                                        true, tid, cpu});
                    }
                } else if (tag == "bad") {
                    if (line >> cpu >> hex >> t1 >> t2 >> dec) {
                        block = make_shared<rtit_block>(rtit_block{
                                                        rtit_block::BAD,
                                                        {},
                                                        {t1, t2},
                                                        true, tid, cpu});
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
            }
        } else {
            SAT_ERR("syntax error in collection: task name\n");
        }
    } else {
        SAT_ERR("syntax error in collection: task ID\n");
    }

    return result;
}

void rtit_task::dump() const
{
    printf("TASK %u\n", imp_->tid_);
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
