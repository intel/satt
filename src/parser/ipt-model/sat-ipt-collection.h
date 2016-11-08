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
#ifndef SAT_IPT_COLLECTION_H
#define SAT_IPT_COLLECTION_H

#include "sat-memory.h"
#include "sat-ipt-task.h"
#include <iostream>
#include <vector>

namespace sat {

using namespace std;

class ipt_collection {
public:
    explicit ipt_collection(const string&         sideband_path,
                            const vector<string>& ipt_paths);
    explicit ipt_collection(istream& is);
    ~ipt_collection();

    operator bool();

    bool serialize(ostream& stream) const;
    const vector<string>& ipt_paths() const;
    const string&         sideband_path() const;
    uint64_t earliest_tsc() const;

    unsigned             tasks() const;
    vector<tid_t>        tids() const;
    shared_ptr<ipt_task> task(tid_t tid) const;
    vector<tid_t>        tids_in_decreasing_order_of_trace_size() const;
private:
    class imp;
    unique_ptr<imp> imp_;
}; // ipt_collection

} // sat

#endif // SAT_IPT_COLLECTION_H
