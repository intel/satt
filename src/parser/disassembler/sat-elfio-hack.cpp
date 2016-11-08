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

// Elfio makes section offset getter protected.
// We need the section offset, so let's hack a workaround to get to it.

// first #include everything that elfio_section.hpp #includes or needs
#include <string>
#include <iostream>
#include <elfio/elf_types.hpp>
#include <elfio/elfio_utils.hpp>

// then redefine 'protected' to 'public' and #include elfio_section.hpp
// to make section offset getter public within this file
#define protected public
#include <elfio/elfio_section.hpp>
#undef  protected

namespace sat {

using namespace ELFIO;

Elf64_Off get_elfio_section_offset(const section* s)
{
    return s->get_offset();
}

}
