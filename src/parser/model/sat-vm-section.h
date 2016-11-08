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
#ifndef SAT_VM_SECTIONS_H
#define SAT_VM_SECTIONS_H

#include "sat-types.h"
#include "sat-tid.h"
#include <map>

namespace sat {

typedef struct {size_t size; rva offset; tid_t tid; string name;} vm_sec_info_type;
typedef std::map<rva /*start address*/, vm_sec_info_type> vm_sec_list_type;

typedef struct {size_t end; string module;} vm_x86_64_info_type;
typedef std::map<rva /*start address*/, vm_x86_64_info_type> vm_x86_64_list_type;

} // namespace sat

#endif /* SAT_VM_SECTIONS_H */
