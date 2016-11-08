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
#ifndef SAT_IPT_TASK_H
#define SAT_IPT_TASK_H

#include "sat-tid.h"
#include "sat-memory.h"
#include <iostream>
#include <functional>

namespace sat {

using namespace std;

class ipt_block;

class ipt_task {
public:
    bool serialize(ostream& stream) const;
    static shared_ptr<ipt_task> deserialize(istream&       stream,
                                            string&        tag,
                                            istringstream& line);
    ipt_task(tid_t tid, const string& name);
    ~ipt_task();

    tid_t tid() const;
    const string& name() const;
    uint64_t size() const;
    bool get_earliest_tsc(uint64_t& tsc) const;
    void append_block(shared_ptr<ipt_block> block);

    using callback_func = function<bool(shared_ptr<ipt_block>)>;
    void iterate_blocks(callback_func callback) const;

    void dump() const;
private:

    class imp;
    unique_ptr<imp> imp_;
}; // ipt_task

} // sat

#endif // SAT_IPT_TASK_H
