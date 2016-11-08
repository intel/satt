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
#ifndef SAT_RTIT_COLLECTION
#define SAT_RTIT_COLLECTION

#include "sat-memory.h"
#include "sat-rtit-task.h"
#include "sat-vm-section.h"
#include <vector>
#include <string>
#include <iostream>

namespace sat {

using namespace std;

class rtit_collection {
public:
    explicit rtit_collection(const string&         sideband_path,
                             const vector<string>& rtit_paths,
                             const vector<string>& vm_binary_paths,
                             const string&         vm_x86_64_path);
    explicit rtit_collection(istream& stream);
    ~rtit_collection();

    string sideband_path() const;
    const vector<string>& rtit_paths() const;
    unsigned tasks() const;
    vector<tid_t> tids() const;
    vector<tid_t> tids_in_decreasing_order_of_rtit_size() const;
    shared_ptr<rtit_task> task(tid_t tid);

    uint64_t earliest_tsc() const;

    bool serialize(ostream& o) const;
    bool deserialize(istream& i);


    vm_sec_list_type& get_vm_sections();
    vm_x86_64_list_type& get_x86_64_func_regions();

    void dump() const;

private:
    void deserialize_vm(istream&       stream,
                        string&        tag,
                        istringstream& line);
    void deserialize_x86_64_func(istream&       stream,
                                 string&        tag,
                                 istringstream& line);
    bool add_vm_file(string vm_path);

    class imp;
    unique_ptr<imp> imp_;
}; // rtit_collection

} // namespace sat

#endif // SAT_RTIT_COLLECTION
