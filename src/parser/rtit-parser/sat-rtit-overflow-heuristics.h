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
#ifndef SAT_RTIT_OVERFLOW_HEURISTICS_H
#define SAT_RTIT_OVERFLOW_HEURISTICS_H

#include "sat-rtit-parser.h"
#include <string>
#include <map>

namespace sat {

using namespace std;

typedef pair<uint64_t /*tsc*/, uint64_t /*error margin*/> overflow_time;
typedef map<rtit_offset, overflow_time> overflow_map;

bool time_overflows(const string& path,
                    bool          skip_to_first_psb,
                    overflow_map& overflows);

}

#endif
