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
#ifndef SAT_HELPER_PATH_MAPPER_H
#define SAT_HELPER_PATH_MAPPER_H

#include "sat-caching-path-mapper.h"
#include <memory>

namespace sat {

class helper_path_mapper : public caching_path_mapper {
public:
    explicit helper_path_mapper(const string& cache_dir_path,
                                const string& helper_command_format_string);
    ~helper_path_mapper();

protected:
    bool get_host_path(const string& target_path,
                       string&       host_path,
                       string&       symbols_path) const override;

private:
    helper_path_mapper();

    class imp;
    unique_ptr<imp> imp_;
}; // helper_path_mapper

} // sat

#endif // SAT_HELPER_PATH_MAPPER_H
