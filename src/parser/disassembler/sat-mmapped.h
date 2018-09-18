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
#ifndef SAT_MMAPPED_H
#define SAT_MMAPPED_H

#include "sat-types.h"
#include <string>
#include <memory>
#include <functional>

namespace sat {

using namespace std;

class mmapped
{
private:
    explicit mmapped(const string& path, const string& sym_path, bool vm=0);

public:
    ~mmapped();

    static shared_ptr<mmapped> obtain(const string& path, const string& sym_path, bool vm);
    static shared_ptr<mmapped> obtain(const string& path, const string& sym_path);

    bool is_ok();

    bool get_host_mmap(const unsigned char*& host_load_address,
                       unsigned&             size,
                       unsigned&             bits) const;

    bool get_function(rva offset, string& name, unsigned& offset_in_func);
    bool get_function(string name, rva& address, size_t& size);
    bool get_global_function(const string& name, rva& offset);
    bool get_relocation(rva offset, string& name);
    bool get_text_section(rva& offset, size_t& size);

    void iterate_functions(function<void(rva           /*offset*/,
                                         size_t        /*size*/,
                                         const string& /*name*/)> callback);

    void iterate_executable_sections(function<void(
                                              rva           /*offset*/,
                                              rva           /*target_address*/,
                                              size_t        /*size*/,
                                              const string& /*name*/)> callback);

    rva default_load_address();

    void add_x86_64_region(rva begin, rva end);
    bool is_x86_64_region(rva address);

    class impl;
private:
    unique_ptr<impl> pimpl_;
};

} // namespace sat

#endif // SAT_MMAPPED_H
