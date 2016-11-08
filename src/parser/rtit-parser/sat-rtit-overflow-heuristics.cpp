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
#include "sat-rtit-overflow-heuristics.h"
#include "sat-rtit-parser.h"
#include "sat-rtit-workarounds.h"
#include "sat-file-input.h"
#include <vector>

namespace sat {

using namespace std;

class overflow_time_estimator : public rtit_parser_output {
public:
    overflow_time_estimator(unsigned initial_rng, overflow_map& overflows) :
        overflows_(overflows),
        rng_(initial_rng)
    {}

    void sts()
    {
        update_tsc(parsed_.sts.tsc);
    }

    void mtc()
    {
        rng_ = parsed_.mtc.rng;

        unsigned new_mtc      = parsed_.mtc.tsc;
        unsigned previous_mtc = (previous_tsc_ >> (7 + 2 * rng_)) & 0xff;
        uint64_t new_tsc      = previous_tsc_;

        if (previous_mtc > new_mtc) {
            new_tsc += 0x100 << (7 + 2 * rng_);
        }
        new_tsc = (new_tsc &
                   (0xffffffffffffffff << (15 + 2 * rng_)))
                | (new_mtc << (7 + 2 * rng_));
        update_tsc(new_tsc);
    }

    void fup_buffer_overflow()
    {
        overflow_queue_.push_back(parsed_.pos.offset_);
    }

private:
    void update_tsc(uint64_t new_tsc)
    {
        // log any dropped MTC's
        unsigned new_mtc      = (new_tsc       >> (7 + 2 * rng_)) & 0xff;
        unsigned previous_mtc = (previous_tsc_ >> (7 + 2 * rng_)) & 0xff;
        unsigned diff;
        if (previous_mtc > new_mtc) {
            diff = 256 - previous_mtc + new_mtc;
        } else {
            diff = new_mtc - previous_mtc;
        }
        if (diff > 1) {
            warning(parsed_.pos, OTHER, "dropped MTC's");
        }

        if (overflow_queue_.size() != 0) {
            // there were overflows since the previous timestamp;
            // estimate when they happened
            uint64_t time_span     = new_tsc - previous_tsc_;
            uint64_t overflow_span = overflow_queue_.size();
            uint64_t overflow_n    = 0;
            for (auto overflow_offset : overflow_queue_) {
                ++overflow_n;
                uint64_t estimated_tsc = previous_tsc_ +
                                         time_span * overflow_n /
                                         (overflow_span + 1);
                uint64_t error_margin = max(estimated_tsc - previous_tsc_,
                                            new_tsc - estimated_tsc);
                // store the estimates in the overflow map
                overflows_.insert({overflow_offset,
                                  {estimated_tsc, error_margin}});
            }
            overflow_queue_.clear();
        }

        previous_tsc_ = new_tsc;
    }

    vector<rtit_offset> overflow_queue_;
    overflow_map&       overflows_;
    uint64_t            previous_tsc_;
    unsigned            rng_; // MTC rng
};


bool time_overflows(const string& path,
                    bool          skip_to_first_psb,
                    overflow_map& overflows)
{
    typedef rtit_parser<postpone_early_mtc<default_rtit_policy>> parser_type;
    typedef file_input<rtit_parser_input>                        input_type;
    typedef overflow_time_estimator                              output_type;

    shared_ptr<input_type> input{new input_type};
    if (!input->open(path)) {
        return false;
    }

    // TODO: get initial rng (3) from sideband
    shared_ptr<output_type> output{new output_type(3, overflows)};

    parser_type parser(input, output);

    return parser.parse(false, skip_to_first_psb);
}

} // namespace sat
