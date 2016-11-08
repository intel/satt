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
#ifndef SAT_OAT_H
#define SAT_OAT_H

#include "sat-types.h"
#include "sat-elfio-hack.h"
#include <elfio/elfio.hpp>
#include <cinttypes>
#include <functional>

namespace sat {

using namespace std;

// symbols in the symbol table (.dynsym section) are as follows:
// - oatdata points to headers, DEX files
// - oatexec points to the compiled code
// - oatlasword is just an end marker (marks the last 4 bytes of oatdata)

struct oat_symbol
{
    Elf64_Addr offset;
    Elf_Xword  size;
    Elf_Half   section_index;
    Elf64_Addr file_offset;
};

bool get_oat_info(istream&    mmapped_file,
                  rva&        default_load_address,
                  rva&        text_section_offset,
                  size_t&     text_section_size,
                  unsigned&   bits,
                  oat_symbol& oatdata,
                  oat_symbol& oatexec,
                  oat_symbol& oatlastword,
                  string&     version);

typedef function<void(rva /*offset*/, size_t /*size*/, const string& /*name*/)>
        oat_method_callback;

void iterate_oat_methods(const uint8_t* oat_data, int version, oat_method_callback callback);

} // namespace sat

#endif // SAT_OAT_H
