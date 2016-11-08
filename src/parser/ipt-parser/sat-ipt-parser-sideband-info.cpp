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
#include "sat-ipt-parser-sideband-info.h"
#include "sat-log.h"
#include "sat-file-input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <dirent.h>
#include <cinttypes>
#include <algorithm>

using namespace sat;
using namespace std;

namespace sat {

    uint32_t init_tsc_ctc_ratio = 1;
    uint8_t  init_mtc_freq = 0;
}

class sideband_info_stdin : public sideband_parser_input {
public:
    bool read(unsigned size, void* buffer)
    {
        auto s = fread(buffer, 1, size, stdin);
        if (s < size) {
            return false; // error: did not get enough data
            // TODO: differentiate between zero and partial reads
        } else {
            return true;
        }
    }

    bool eof() { return feof(stdin); }

    bool bad() { return ferror(stdin); }
};


class sideband_info_collector : public sideband_parser_output {
public:
        sideband_info_collector() {}

        virtual void init(const sat_header& header,
                          pid_t             pid,
                          pid_t             thread_id,
                          unsigned          tsc_tick,
                          unsigned          fsb_mhz,
                          uint32_t          tma_ratio_tsc,
                          uint32_t          tma_ratio_ctc,
                          uint8_t           mtc_freq) override
        {
            if (tma_ratio_tsc && tma_ratio_ctc) {
                init_tsc_ctc_ratio = (uint32_t) (tma_ratio_tsc / tma_ratio_ctc);
            }
            init_mtc_freq = mtc_freq;
        };

}; // class sideband_info_collector

namespace sat {

        uint32_t sideband_info::tsc_ctc_ratio() const
        {
            return init_tsc_ctc_ratio;
        }

        uint8_t sideband_info::mtc_freq() const
        {
            return init_mtc_freq;
        }

        bool sideband_info::build(const string& sideband_path)
        {
            bool built = false;
            using sideband_info_input = file_input<sideband_parser_input>;
            shared_ptr<sideband_info_input> input{new sideband_info_input};
            if (input->open(sideband_path)) {
                shared_ptr<sideband_info_collector>
                    output{new sideband_info_collector};

                sideband_parser parser(input, output);

                if (parser.parse()) {
                    built = true;
                } else {
                    SAT_ERR("# sideband model building failed\n");
                }
            } else {
                SAT_ERR("# cannot open sideband file for input: '%s'\n",
                        sideband_path.c_str());
            }

            return built;
        }

} // namespace sat
