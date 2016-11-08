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
#ifndef SAT_IPT_ITERATOR_H
#define SAT_IPT_ITERATOR_H

#include "sat-ipt-parser.h"
#include "sat-input.h"
#include "sat-memory.h"
#include <functional>

namespace sat {

using namespace std;

template <class INPUT>
class ipt_token_output {
public:
    ipt_token_output(const INPUT&) {}

    using token = ipt_parser_token<ipt_token_output>;
    void short_tnt(token&) {}
    void long_tnt (token&) {}
    void tip      (token&) {}
    void tip_pge  (token&) {}
    void tip_pgd  (token&) {}
    void fup      (token&) {}
    void pip      (token&) {}
    void tsc      (token&) {}
    void mtc      (token&) {}
    void mode_exec(token&) {}
    void mode_tsx (token&) {}
    void tracestop(token&) {}
    void cbr      (token&) {}
    void tma      (token&) {}
    void cyc      (token&) {}
    void vmcs     (token&) {}
    void ovf      (token&) {}
    void psb      (token&) {}
    void psbend   (token&) {}
    void mnt      (token&) {}
    void pad      (token&) {}
    void eof      (token&) {}
    void report_warning(const string& message) {}
    void report_error(const string& message) {}
    void set_ff_state(bool state) {}
}; // ipt_token_output

class ipt_iterator {
public:
    explicit ipt_iterator(const string& path);
    ~ipt_iterator();

    using output = ipt_token_output<input_from_file>;
    using token = output::token;
    using callback_func = function<void(const token&, size_t offset)>;
    bool iterate(callback_func callback);

private:
    class imp;
    unique_ptr<imp> imp_;
}; // ipt_iterator

} // sat

#endif // SAT_IPT_ITERATOR_H
