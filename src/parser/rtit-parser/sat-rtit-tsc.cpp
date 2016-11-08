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
#include "sat-rtit-tsc.h"
#include "sat-file-input.h"

namespace sat {

class tsc_collector : public rtit_parser_output {
public:
    tsc_collector(unsigned initial_rng, uint64_t initial_tsc) :
        rng_(initial_rng),
        tsc_(initial_tsc)
    {}

    void sts()
    {
        update_tsc(parsed_.pos.offset_, parsed_.sts.tsc);
    }

    void mtc()
    {
        rng_ = parsed_.mtc.rng;

        unsigned new_mtc      = parsed_.mtc.tsc;
        unsigned previous_mtc = (tsc_ >> (7 + 2 * rng_)) & 0xff;
        uint64_t new_tsc      = tsc_;

        if (previous_mtc > new_mtc) {
            new_tsc += 0x100 << (7 + 2 * rng_);
        }
        new_tsc = (new_tsc &
                   (0xffffffffffffffff << (15 + 2 * rng_)))
            | (new_mtc << (7 + 2 * rng_));
        update_tsc(parsed_.pos.offset_, new_tsc);
    }

    rtit_offset offset() const { return offset_; }
    uint64_t    tsc()    const { return tsc_; }

private:
    void update_tsc(rtit_offset new_offset, uint64_t new_tsc)
    {
        unsigned new_mtc      = (new_tsc >> (7 + 2 * rng_)) & 0xff;
        unsigned previous_mtc = (tsc_    >> (7 + 2 * rng_)) & 0xff;
        unsigned diff;
        if (previous_mtc > new_mtc) {
            diff = 256 - previous_mtc + new_mtc;
        } else {
            diff = new_mtc - previous_mtc;
        }
        if (diff > 1) {
            warning(parsed_.pos, OTHER, "dropped MTC's");
        }

        offset_ = new_offset;
        tsc_    = new_tsc;

        stop_parsing();
    }

    unsigned    rng_; // MTC rng
    rtit_offset offset_;
    uint64_t    tsc_;
};


tsc_iterator::tsc_iterator(const string& path,
                           bool          skip_to_first_psb,
                           unsigned      initial_rng,
                           uint64_t      initial_tsc) :
    skip_to_first_psb_(skip_to_first_psb),
    ok_(false)
{
    typedef file_input<rtit_parser_input> input_type;
    typedef tsc_collector                 output_type;

    shared_ptr<input_type> input{new input_type};
    if (input->open(path)) {
        shared_ptr<output_type> output{new output_type(initial_rng,
                                                       initial_tsc)};

        output_ = output.get();
        parser_ = make_shared<parser_type>(input, output);
        ok_ = true;
    }
}

tsc_iterator::operator bool()
{
    return ok_;
}

bool tsc_iterator::get_next(rtit_offset& next_offset, uint64_t& next_tsc)
{
    bool got_tsc = false;

    if (parser_->parse(false, skip_to_first_psb_)) {
        next_offset = output_->offset();
        next_tsc    = output_->tsc();
        got_tsc     = true;
    }

    return got_tsc;
}


tsc_span_iterator::tsc_span_iterator(const string& path,
                                     bool          skip_to_first_psb,
                                     unsigned      initial_rng,
                                     uint64_t      initial_tsc) :
    iterator_(path, skip_to_first_psb, initial_rng, initial_tsc),
    next_offset_(),
    next_tsc_(initial_tsc)
{
}

tsc_span_iterator::operator bool()
{
    return iterator_;
}

bool tsc_span_iterator::get_next(rtit_offset& next_offset, size_t& rtit_span,
                                 uint64_t&    next_tsc,    size_t& tsc_span)
{
    bool got_span = false;

    next_offset = next_offset_;
    next_tsc    = next_tsc_;

    if (iterator_.get_next(next_offset_, next_tsc_)) {
        rtit_span = next_offset_ - next_offset;
        tsc_span  = next_tsc_    - next_tsc;
        got_span = true;
    }

    return got_span;
}


bool tsc_span_base::open_rtit_file(const string& path,
                                   bool          skip_to_first_psb,
                                   unsigned      initial_rng,
                                   uint64_t      initial_tsc)
{
    spans_ = make_shared<tsc_span_iterator>(path,
                                            skip_to_first_psb,
                                            initial_rng,
                                            initial_tsc);
    ok_ = *spans_;

    return ok_;
}

} // namespace sat
