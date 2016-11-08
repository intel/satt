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
#ifndef SAT_RTIT_FILE
#define SAT_RTIT_FILE

#include "sat-rtit-block.h"
#include "sat-sideband-model.h"
#include <memory>
#include <string>
#include <functional>


namespace sat {

using namespace std;

#if 0
class rtit_block_iterator : public iterator<input_iterator_tag,
                                            shared_ptr<rtit_block>>
{
public:
    rtit_block_iterator(vector<shared_ptr<rtit_block>>::iterator i) : i_(i) {}
    rtit_block_iterator(const rtit_block_iterator& rbi) : i_(rbi.i_) {}
    rtit_block_iterator& operator++() { ++i_; return *this; }
    rtit_block_iterator operator++(int) {
        rtit_block_iterator tmp(*this);
        operator ++();
        return tmp;
    }
    bool operator ==(const rtit_block_iterator& rhs) { return i_ == rhs.i_; }
    bool operator !=(const rtit_block_iterator& rhs) { return i_ != rhs.i_; }
    shared_ptr<rtit_block>& operator *() { return *i_; }
private:
    vector<shared_ptr<rtit_block>>::iterator i_;
}; // rtit_block_iterator
#endif


class rtit_file {
public:
    explicit rtit_file(unsigned                         cpu,
                       const string&                    path,
                       shared_ptr<const sideband_model> sideband,
                       const vm_sec_list_type& vm_section_list);
    ~rtit_file();

    string path() const;

    using callback_func = function<void(shared_ptr<rtit_block>)>;
    void iterate_blocks(callback_func callback) const;

    shared_ptr<rtit_block> begin()   const;
    shared_ptr<rtit_block> current() const;
    void                   advance() const;

    void dump();

private:
    class imp;
    unique_ptr<imp> imp_;
}; // class rtit_file

} // namespace sat

#endif // SAT_RTIT_FILE
