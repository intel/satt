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
#ifndef SAT_LOCAL_PATH_MAPPER_H
#define SAT_LOCAL_PATH_MAPPER_H

#include "sat-path-mapper.h"
#include <string>
#include <vector>
#include <memory>

namespace sat {

using namespace std;

class local_path_mapper : path_mapper {
public:
    explicit local_path_mapper(const vector<string>& haystacks);
    ~local_path_mapper();

    bool find_file(const string& file, string& result, string& syms) const;
    bool find_kernel_module(const string& module, string& result) const;
    bool find_dir(const string& path, string& result) const; // TODO: remove

private:
    local_path_mapper();

    class imp;
    unique_ptr<imp> imp_;
};

}

#endif // SAT_LOCAL_PATH_MAPPER_H
