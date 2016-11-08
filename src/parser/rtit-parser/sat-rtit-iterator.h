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
#ifndef SAT_RTIT_ITERATOR_H
#define SAT_RTIT_ITERATOR_H

#include "sat-rtit-parser.h"
#include <functional>
#include <memory>

namespace sat {

using namespace std;

class rtit_iterator {
public:
    explicit rtit_iterator(const string& path);
    ~rtit_iterator();
    typedef function<void(const rtit_parser_output::item&)> callback_func;
    typedef function<void(const rtit_parser_output::item&, const rtit_offset&)> callback_func_pkt_cnt;
    bool iterate(callback_func callback);
    bool iterate(callback_func_pkt_cnt callback);

private:
    class impl;
    unique_ptr<impl> pimpl_;
}; // class rtit_iterator

} // namespace sat

#endif // SAT_RTIT_ITERATOR_H
