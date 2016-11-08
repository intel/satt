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
#include "sat-ipt-collection.h"
#include "sat-ipt-task.h"
#include "sat-getline.h"
#include "sat-sideband-model.h"
#include "sat-ipt-file.h"
#include "sat-log.h"
#include <map>
#include <set>

namespace sat {

namespace {

string task_name(tid_t tid, shared_ptr<sideband_model> sideband)
{
    std::ostringstream name;
    pid_t              pid;
    pid_t              thread_id;
    unsigned           cpu;

    if (tid_get_info(tid, pid, thread_id, cpu)) {
        string t;
        if (sideband->get_thread_name(thread_id, t)) {
            // task has its own name; use it
            name << t;
        } else {
            // task does not have a name; use the name of the process
            name << sideband->process(pid);
        }

        if (thread_id == 0) {
            // identify which cpu's swapper task this is
            name << "/" << cpu;
        }
    }

    return name.str();
}

} // anonymous namespace

struct ipt_collection::imp {
    bool serialize(ostream& stream) const;
    bool deserialize(istream& is);

    bool                             ok_;
    vector<string>                   ipt_paths_;
    string                           sideband_path_;
    map<tid_t, shared_ptr<ipt_task>> tasks_;
}; // ipt_collection::imp

bool ipt_collection::imp::serialize(ostream& stream) const
{
    stream << "# " << ipt_paths_.size() << " IPT files:" << endl;
    for (const auto& f : ipt_paths_) {
        stream << "trace " << quote(f) << endl;
    }
    stream << "# sideband file:" << endl;
    stream << "sideband " << quote(sideband_path_) << endl;

    stream << "# " << tasks_.size() << " traced tasks:" << endl;
    for (const auto& t : tasks_) {
        stream << "task ";
        t.second->serialize(stream);
    }

    stream << "eof" << endl;

    return true; // TODO
}

bool ipt_collection::imp::deserialize(istream& is)
{
    bool ok = true;

    string        tag;
    istringstream line;
    if (get_tagged_line(is, tag, line)) {
        while (ok) {
            if (tag == "trace") {
                string path;
                if (dequote(line, path)) {
                    ipt_paths_.push_back(path);
                    if (!(ok = get_tagged_line(is, tag, line))) {
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false;
                    SAT_ERR("syntax error in collection: IPT path\n");
                }
            } else if (tag == "sideband") {
                if (dequote(line, sideband_path_)) {
                    if (!(ok = get_tagged_line(is, tag, line))) {
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false;
                    SAT_ERR("syntax error in collection: sideband path\n");
                }
            } else if (tag == "task") {
                shared_ptr<ipt_task> task;
                if ((task = ipt_task::deserialize(is, tag, line))) {
                    tasks_.insert({task->tid(), task});
                    if (tag == "") {
                        // task hit EOF
                        ok = false;
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false; // error message already output by task
                }
            } else if (tag == "eof") {
                break; // successfuly done
            } else {
                ok = false;
                SAT_ERR("syntax error in ipt collection: unknown tag '%s'\n",
                        tag.c_str());
            }
        } // while
    } else {
        ok = false;
        SAT_ERR("could not deserialize collection\n");
    }

    return ok;
}

ipt_collection::ipt_collection(const string&         sideband_path,
                               const vector<string>& ipt_paths) :
    imp_(make_unique<imp>())
{
    // build sideband
    imp_->sideband_path_ = sideband_path;
    auto sideband = make_shared<sideband_model>();
    sideband->build(sideband_path);

    imp_->ipt_paths_ = ipt_paths;
// TODO should this be in IMP ?? bstorola
    vector<shared_ptr<ipt_file>> ipt_files;
    unsigned cpu = 0;
    for (const auto& ipt_path : ipt_paths) {
        ipt_files.push_back(make_shared<ipt_file>(cpu, ipt_path, sideband, sideband_path));
        ++cpu;
    }

    // sort IPT blocks into tasks
    // first initialize all files for iteration
    for (auto f = ipt_files.begin(); f != ipt_files.end(); ++f) {
        if (!(*f)->begin()) {
            f = ipt_files.erase(f);
            if (f == ipt_files.end()) {
                break;
            }
        }
    }

    // then merge blocks from ipt files into tasks
    while (ipt_files.size()) {
        // find the two ipt files with the lowest timestamps
        auto first = ipt_files.end();
        auto next  = ipt_files.end();
        for (auto f = ipt_files.begin(); f != ipt_files.end(); ++f) {
            if (first == ipt_files.end() ||
                (*f)->current()->tsc_.first < (*first)->current()->tsc_.first)
            {
                next = first;
                first = f;
            } else if (next == ipt_files.end() ||
                       (*f)->current()->tsc_.first <
                           (*next)->current()->tsc_.first)
            {
                next = f;
            }
        }

        // insert blocks from the ipt file with the lowest timestamp to tasks
        do {
            shared_ptr<ipt_task> task;
            auto tid = (*first)->current()->tid_;
            auto t = imp_->tasks_.find(tid);
            if (t == imp_->tasks_.end()) {
                task = make_shared<ipt_task>(tid, task_name(tid, sideband));
                imp_->tasks_.insert({tid, task});
            } else {
                task = t->second;
            }
            task->append_block((*first)->current());
            (*first)->advance();
            if (!(*first)->current()) {
                ipt_files.erase(first);
                break;
            }
        } while (next == ipt_files.end() ||
                 (*first)->current()->tsc_.first <=
                     (*next)->current()->tsc_.first);

    }

}

ipt_collection::ipt_collection(istream& is) :
    imp_(make_unique<imp>())
{
    imp_->ok_ = imp_->deserialize(is);
}

ipt_collection::~ipt_collection()
{
}

ipt_collection::operator bool()
{
    return imp_->ok_;
}

bool ipt_collection::serialize(ostream& stream) const
{
    return imp_->serialize(stream);
}

const vector<string>& ipt_collection::ipt_paths() const
{
    return imp_->ipt_paths_;
}

const string& ipt_collection::sideband_path() const
{
    return imp_->sideband_path_;
}

unsigned ipt_collection::tasks() const
{
    return imp_->tasks_.size();
}

vector<tid_t> ipt_collection::tids() const
{
    vector<tid_t> result;
    for (const auto& t : imp_->tasks_) {
        result.push_back(t.first);
    }
    return result;
}

vector<tid_t> ipt_collection::tids_in_decreasing_order_of_trace_size() const
{
    // first make a set that orders (size, tid) pairs
    using st = pair<size_t, tid_t>;
    auto comp = [](st a, st b) {
        return a.first > b.first || a.second < b.second;
    };
    set<st, decltype(comp)> tmp(comp);

    // then populate the set
    for (const auto& t : imp_->tasks_) {
        tmp.insert({t.second->size(), t.first});
    }

    // finally, pull the tids from the set into a vector
    vector<tid_t> result;
    for (const auto& t : tmp) {
        result.push_back(t.second);
    }

    return result;
}

shared_ptr<ipt_task> ipt_collection::task(tid_t tid) const
{
    shared_ptr<ipt_task> result;
    auto t = imp_->tasks_.find(tid);
    if (t != end(imp_->tasks_)) {
        result = t->second;
    }
    return result;
}

uint64_t ipt_collection::earliest_tsc() const
{
    uint64_t result = 0;

    for (const auto& t : imp_->tasks_) {
        uint64_t tsc;
        if (t.second->get_earliest_tsc(tsc) && (result == 0 || tsc < result)) {
            result = tsc;
        }
    }

    return result;
}

} // sat
