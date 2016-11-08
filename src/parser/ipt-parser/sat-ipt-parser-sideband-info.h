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
#ifndef SAT_IPT_PARSER_SIDEBAND_INFO_H
#define SAT_IPT_PARSER_SIDEBAND_INFO_H

#include "sat-sideband-parser.h"
#include <string>
#include <memory>
#include <functional>

namespace sat {

    using namespace std;

    class sideband_info
    {
    public:
        bool build(const string& sideband_path);
        uint32_t tsc_ctc_ratio() const;
        uint8_t mtc_freq() const;

    };

}

#endif
