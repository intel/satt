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
#ifndef SAT_FILESYSTEM_H
#define SAT_FILESYSTEM_H

#include <string>
#include <functional>

namespace sat {

using namespace std;

bool find_path(const string&                       needle,
               const string&                       haystack,
               function<void(const string& match)> callback);

bool is_dir(const string& path);
bool recurse_dir(const string&                                         path,
                 function<bool(const string& dir, const string& file)> callback);

unsigned file_size(const string& path);

} // namespace sat

#endif
