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
#ifndef SAT_IPT_FILE_H
#define SAT_IPT_FILE_H

#include "sat-ipt-block.h"
#include "sat-sideband-model.h"
#include <limits>

namespace sat {

using namespace std;

class ipt_file {
public:
    ipt_file(unsigned                         cpu,
             const string&                    path,
             shared_ptr<const sideband_model> sideband,
             const string&                    sideband_path);
    shared_ptr<ipt_block> begin() const;
    shared_ptr<ipt_block> current() const;
    void advance() const;
    ~ipt_file();

private:
    class imp;
    unique_ptr<imp> imp_;
}; // ipt_file

} // sat

#endif // SAT_IPT_FILE_H
