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
#include "sat-rtit-collection.h"
#include "sat-rtit-task.h"
#include "sat-rtit-file.h"
#include "sat-sideband-model.h"
#include "sat-mmapped.h"
#include "sat-getline.h"
#include "sat-log.h"
#include <map>
#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cctype>

namespace sat {

using namespace std;

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

struct rtit_collection::imp {
    string                            sideband_path_;
    vector<string>                    rtit_paths_;
    vector<string>                    vm_binary_paths_;
    map<tid_t, shared_ptr<rtit_task>> tasks_;
    vm_sec_list_type                  vm_sections_;
    vm_x86_64_list_type               vm_x86_64_funcs_;
}; // rtit_collection::imp

rtit_collection::rtit_collection(const string&         sideband_path,
                                 const vector<string>& rtit_paths,
                                 const vector<string>& vm_binary_paths,
                                 const string&         vm_x86_64_path) :
    imp_(make_unique<imp>())
{
    // Collect executable sections from vm binaries
    imp_->vm_binary_paths_ = vm_binary_paths;
    for (auto& vm_file : vm_binary_paths) {
        add_vm_file(vm_file);
    }

#if 0
    // Create vm x86_64 function map
    if (!vm_x86_64_path.empty())
    {
        ifstream ifs (vm_x86_64_path);
        if (ifs.is_open()) {
            string func, path;
            map<string /*name*/, string /*path*/> vm_files;
            vm_x86_64_list_type tmp_funcs;
            while (ifs.good()) {
                if(ifs.peek() == '#') {
                    ifs.ignore(256, '\n');
                } else {
                    ifs >> func >> path;
                    if (!func.empty() && !path.empty()) {
                        size_t found = string::npos;
                        if (vm_files.count(path) == 0) {
                            for (auto i=vm_binary_paths.begin(); i!=vm_binary_paths.end(); ++i) {
                                found = i->find(path);
                                if (found != string::npos) {
                                    vm_files[path] = *i;
                                    path = *i;
                                    break;
                                }
                            }
                        } else {
                            path = vm_files[path];
                            found = 1;
                        }
                        if (found != string::npos) {
                            shared_ptr<mmapped> vm_map =
                                mmapped::obtain(path, path, /* VM */ true);
                            rva address;
                            size_t size;
                            if(vm_map->get_function(func, address, size)) {
                                tmp_funcs[address] = {address+size, path};
                            }
                        } else {
                            fprintf(stderr, "Can't find binary file '%s' for generating x86_64 func list\n",
                                    path.c_str());
                        }
                    }
                }
            }

            // Concatenate ranges of consecutive functions
            if (!tmp_funcs.empty()) {
                auto next = tmp_funcs.begin();
                auto prev = next++;
                rva start = prev->first;
                imp_->vm_x86_64_funcs_[start] = {prev->second.end, prev->second.module};
                while (next != tmp_funcs.end())
                {
                    if (!next->second.module.compare(prev->second.module) &&
                         next->first != prev->second.end) {
                        // non-consecutive regions, update new start address
                        start = next->first;
                    }
                    imp_->vm_x86_64_funcs_[start] = {next->second.end, next->second.module};
                    prev = next++;
                }
            }

        }
        else {
            fprintf(stderr, "Can't open vm x86_64 func file '%s'\n", vm_x86_64_path.c_str());
        }
    }
#endif

    // build sideband
    imp_->sideband_path_ = sideband_path;
    auto sideband = make_shared<sideband_model>();
    sideband->build(sideband_path, get_vm_sections());

    // run heuristics on RTIT
    imp_->rtit_paths_ = rtit_paths;
    vector<shared_ptr<rtit_file>> rtit_files;
    unsigned cpu = 0;
    for (const auto& rtit_path : rtit_paths) {
        rtit_files.push_back(make_shared<rtit_file>(cpu, rtit_path, sideband, get_vm_sections()));
        ++cpu;
    }

    // sort RTIT blocks into tasks
    // first initialize all files for iteration
    for (auto f = rtit_files.begin(); f != rtit_files.end(); ++f) {
        if (!(*f)->begin()) {
            f = rtit_files.erase(f);
            if (f == rtit_files.end()) {
                break;
            }
        }
    }
    // then merge blocks from rtit files into tasks
    while (rtit_files.size()) {
        // find the two RTIT files with the lowest timestamps
        auto first = rtit_files.end();
        auto next  = rtit_files.end();
        for (auto f = rtit_files.begin(); f != rtit_files.end(); ++f) {
            if (first == rtit_files.end() ||
                (*f)->current()->tsc_.first < (*first)->current()->tsc_.first)
            {
                next = first;
                first = f;
            } else if (next == rtit_files.end() ||
                       (*f)->current()->tsc_.first <
                           (*next)->current()->tsc_.first)
            {
                next = f;
            }
        }

        // insert blocks from the RTIT file with the lowest timestamp to tasks
        do {
            shared_ptr<rtit_task> task;
            auto tid = (*first)->current()->tid_;
            auto t = imp_->tasks_.find(tid);
            if (t == imp_->tasks_.end()) {
                task = make_shared<rtit_task>(tid, task_name(tid, sideband));
                imp_->tasks_.insert({tid, task});
            } else {
                task = t->second;
            }
            task->append_block((*first)->current());
            (*first)->advance();
            if (!(*first)->current()) {
                rtit_files.erase(first);
                break;
            }
        } while (next == rtit_files.end() ||
                 (*first)->current()->tsc_.first <=
                     (*next)->current()->tsc_.first);
    }
}

rtit_collection::rtit_collection(istream& stream) :
    imp_(make_unique<imp>())
{
    deserialize(stream);
}

rtit_collection::~rtit_collection()
{
}

string rtit_collection::sideband_path() const
{
    return imp_->sideband_path_;
}

const vector<string>& rtit_collection::rtit_paths() const
{
    return imp_->rtit_paths_;
}

unsigned rtit_collection::tasks() const
{
    return imp_->tasks_.size();
}

vector<tid_t> rtit_collection::tids() const
{
    vector<tid_t> result;
    for (const auto& t : imp_->tasks_) {
        result.push_back(t.first);
    }
    return result;
}

vector<tid_t> rtit_collection::tids_in_decreasing_order_of_rtit_size() const
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

shared_ptr<rtit_task> rtit_collection::task(tid_t tid)
{
    shared_ptr<rtit_task> result;

    auto t = imp_->tasks_.find(tid);
    if (t != end(imp_->tasks_)) {
        result = t->second;
    }

    return result;
}

uint64_t rtit_collection::earliest_tsc() const
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

bool rtit_collection::serialize(ostream& stream) const
{
    stream << "# " << imp_->rtit_paths_.size() << " VM sections:" << endl;
    for (const auto& vm : imp_->vm_sections_) {
        stream << "vm_section " << hex << vm.first
               << " " << hex << vm.second.size
               << " " << hex << vm.second.offset
               << " " << dec << vm.second.tid
               << " " << quote(vm.second.name) << endl;
    }
    stream << "# " << imp_->rtit_paths_.size() << " VM x86_64 functions:" << endl;
    for (const auto& vm : imp_->vm_x86_64_funcs_) {
        stream << "vm_x86_64_func " << hex << vm.first
               << " " << hex << vm.second.end
               << " " << quote(vm.second.module) << endl;
    }
    stream << "# " << imp_->rtit_paths_.size() << " RTIT files:" << endl;
    for (const auto& f : imp_->rtit_paths_) {
        stream << "rtit " << quote(f) << endl;
    }
    stream << "# sideband file:" << endl;
    stream << "sideband " << quote(imp_->sideband_path_) << endl;

    stream << "# " << imp_->tasks_.size() << " traced tasks:" << endl;
    for (const auto& t : imp_->tasks_) {
        stream << "task ";
        t.second->serialize(stream);
    }

    stream << "eof" << endl;

    return true; // TODO
}

void rtit_collection::deserialize_vm(istream&       stream,
                                     string&        tag,
                                     istringstream& line)
{
    rva start, size, offset;
    tid_t tid;
    string name;

    while (tag == "vm_section") {
        line >> hex >> start >> hex >> size >> hex >> offset >> dec >> tid;
        dequote(line, name);
        imp_->vm_sections_.insert({start, {size, offset, tid, name}});
        if (!get_tagged_line(stream, tag, line)) {
            SAT_ERR("syntax error in collection: no EOF tag\n");
        }
    }
}

void rtit_collection::deserialize_x86_64_func(istream&       stream,
                                              string&        tag,
                                              istringstream& line)
{
    rva start, end;
    string func, module;

    while (tag == "vm_x86_64_func") {
        line >> hex >> start >> hex >> end;
        dequote(line, module);
        imp_->vm_x86_64_funcs_.insert({start, {end, module}});
        if (!get_tagged_line(stream, tag, line)) {
            SAT_ERR("syntax error in collection: no EOF tag\n");
        }
    }
}

bool rtit_collection::add_vm_file(string vm_path)
{
    shared_ptr<mmapped> vm_map =
        mmapped::obtain(vm_path, vm_path, /* VM */ true);

    vm_map->iterate_executable_sections([&](rva           offset,
                                            rva           target_address,
                                            size_t        size,
                                            const string& name)
    {
        imp_->vm_sections_[target_address] = {size, offset, 0xFFFFFFFF, vm_path};
    });
    return true;
}

vm_sec_list_type& rtit_collection::get_vm_sections()
{
    return imp_->vm_sections_;
}

vm_x86_64_list_type& rtit_collection::get_x86_64_func_regions()
{
    return imp_->vm_x86_64_funcs_;
}



bool rtit_collection::deserialize(istream& i)
{
    bool ok = true;

    string        tag;
    istringstream line;
    if (get_tagged_line(i, tag, line)) {
        while (ok) {
            if (tag == "rtit") {
                string path;
                if (dequote(line, path)) {
                    imp_->rtit_paths_.push_back(path);
                    if (!(ok = get_tagged_line(i, tag, line))) {
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false;
                    SAT_ERR("syntax error in collection: RTIT path\n");
                }
            } else if (tag == "sideband") {
                if (dequote(line, imp_->sideband_path_)) {
                    if (!(ok = get_tagged_line(i, tag, line))) {
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false;
                    SAT_ERR("syntax error in collection: sideband path\n");
                }
            } else if (tag == "task") {
                shared_ptr<rtit_task> task;
                if ((task = rtit_task::deserialize(i, tag, line))) {
                    imp_->tasks_.insert({task->tid(), task});
                    if (tag == "") {
                        // task hit EOF
                        ok = false;
                        SAT_ERR("syntax error in collection: no EOF tag\n");
                    }
                } else {
                    ok = false; // error message already output by task
                }
            } else if (tag == "vm_section") {
                // populate vm_section list
                deserialize_vm(i, tag, line);
                if (tag == "") {
                    ok = false;
                    SAT_ERR("syntax error in collection: no EOF tag\n");
                }
            } else if (tag == "vm_x86_64_func") {
                // populate vm_x86_64_func list
                deserialize_x86_64_func(i, tag, line);
                if (tag == "") {
                    ok = false;
                    SAT_ERR("syntax error in collection: no EOF tag\n");
                }
            } else if (tag == "eof") {
                break; // successfully done
            } else {
                ok = false;
                SAT_ERR("syntax error in collection: unknown tag '%s'\n",
                        tag.c_str());
            }
        }
    } else {
        ok = false;
        SAT_ERR("could not deserialize collection\n");
    }
    return ok;
}



void rtit_collection::dump() const
{
    for (const auto& task : imp_->tasks_) {
        task.second->dump();
    }
}

} // namespace sat
