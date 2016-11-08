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
#ifndef SAT_RTIT_TSC_H
#define SAT_RTIT_TSC_H

#include "sat-rtit-parser.h"
#include "sat-rtit-workarounds.h"

namespace sat {

using namespace std;

class tsc_iterator {
public:
    tsc_iterator(const string& path,
                 bool          skip_to_first_psb,
                 unsigned      initial_rng,
                 uint64_t      initial_tsc = 0);
    operator bool();

    bool get_next(rtit_offset& next_offset, uint64_t& next_tsc);

private:
    typedef rtit_parser<postpone_early_mtc<default_rtit_policy>> parser_type;

    shared_ptr<parser_type>    parser_;
    const class tsc_collector* output_;
    bool                       skip_to_first_psb_;
    bool                       ok_;
};

class tsc_span_iterator {
public:
    tsc_span_iterator(const string& path,
                      bool          skip_to_first_psb,
                      unsigned      initial_rng,
                      uint64_t      initial_tsc = 0);
    operator bool();

    bool get_next(rtit_offset& next_offset, size_t& rtit_span,
                  uint64_t&    next_tsc,    size_t& tsc_span);

private:
    tsc_iterator iterator_;
    rtit_offset  next_offset_;
    uint64_t     next_tsc_;
};


class tsc_span_base {
public:
    tsc_span_base() :
        ok_()
    {}

    bool open_rtit_file(const string& path,
                        bool          skip_to_first_psb,
                        unsigned      initial_rng,
                        uint64_t      initial_tsc = 0);

protected:
    bool                          ok_;
    shared_ptr<tsc_span_iterator> spans_;
};

template <class PARENT_POLICY>
class tsc_span : public PARENT_POLICY, public tsc_span_base {
public:
    tsc_span(shared_ptr<rtit_parser_output> discard) :
        PARENT_POLICY(discard),
        offset_(), rtit_span_(), tsc_(), tsc_span_()
    {}

    void emit(rtit_parser_output* output)
    {
        while (ok_ &&
               output->parsed_.pos.offset_ >= offset_ + rtit_span_ &&
               spans_->get_next(offset_, rtit_span_, tsc_, tsc_span_))
        {
            // EMPTY LOOP
        }
        PARENT_POLICY::emit(output);
    }

    void get_tsc_span(uint64_t& tsc, size_t& span)
    {
        if (tsc_span_ == 0) {
            // get the initial span
            spans_->get_next(offset_, rtit_span_, tsc_, tsc_span_);
        }

        tsc  = tsc_;
        span = tsc_span_;
    }

private:
    rtit_offset offset_;
    size_t      rtit_span_;
    uint64_t    tsc_;
    size_t      tsc_span_;
};

} // namespace sat

#endif
