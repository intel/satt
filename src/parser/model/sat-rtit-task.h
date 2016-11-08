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
#ifndef SAT_RTIT_TASK
#define SAT_RTIT_TASK

#include "sat-tid.h"
#include <memory>
#include <iostream>
#include <functional>

namespace sat {

using namespace std;

class rtit_block;

class rtit_task {
public:
    static shared_ptr<rtit_task> deserialize(istream&       stream,
                                             string&        tag,
                                             istringstream& line);
    explicit rtit_task(tid_t tid, const string& name);
    ~rtit_task();

    tid_t         tid() const;
    const string& name() const;
    uint64_t      size() const;
    bool          get_earliest_tsc(uint64_t& tsc) const;
    void          set_vmm_host();
    bool          is_vmm_host();

    void append_block(shared_ptr<rtit_block> block);

    using callback_func = function<bool(shared_ptr<rtit_block>)>;
    void iterate_blocks(callback_func callback) const;

    bool serialize(ostream& stream) const;

    void dump() const;

private:
    class imp;
    unique_ptr<imp> imp_;
}; // rtit_task

} // namespace sat

#endif // SAT_RTIT_TASK
