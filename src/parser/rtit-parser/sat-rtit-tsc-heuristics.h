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
#ifndef SAT_RTIT_TSC_HEURISTICS_H
#define SAT_RTIT_TSC_HEURISTICS_H

#include "sat-rtit-parser.h"
#include <functional>
#include <string>
#include <utility>

namespace sat {

class tsc_heuristics_imp;

class tsc_heuristics {
public:
    tsc_heuristics();
   ~tsc_heuristics();

    bool parse_rtit(const std::string& path);
    void apply();
    bool get_initial_tsc(rtit_pos& pos, uint64_t& initial_tsc) const;
    bool get_tsc(rtit_pos pos, uint64_t& tsc) const;
    bool get_tsc(rtit_pos pos, uint64_t& tsc, uint64_t& next_tsc) const;
    bool get_next_valid_tsc(rtit_pos  current_pos,
                            rtit_pos& next_pos,
                            uint64_t& next_tsc);

    using callback_func = function<void(pair<rtit_pos, rtit_pos> pos,
                                        bool                     has_tsc,
                                        pair<uint64_t, uint64_t> tsc)>;
    void iterate(callback_func callback) const;

    void dump() const; // TODO: remove

private:
    shared_ptr<tsc_heuristics_imp> imp_;
};

inline bool is_timing_packet(rtit_parser_output::token_func f)
{
  return f == &rtit_parser_output::sts ||
         f == &rtit_parser_output::mtc ||
         f == &rtit_parser_output::fup_buffer_overflow ||
         f == &rtit_parser_output::fup_pge;
}

template <class PARENT_POLICY = default_rtit_policy>
class skip_to_next_timing_packet_on_request : public PARENT_POLICY {
public:
    skip_to_next_timing_packet_on_request(
        shared_ptr<rtit_parser_output> discard) :
        PARENT_POLICY(discard),
        skipping_(false)
    {}

    void emit(rtit_parser_output* output)
    {
        rtit_parser_output::token_func f = output->parsed_.token;

        if (!skipping_) {
            PARENT_POLICY::emit(output);
        } else {
            uint64_t a, b;
            if (f == &rtit_parser_output::psb) {
                PARENT_POLICY::emit(output);
            } else if ((is_timing_packet(f) ||
                        output->parsed_.pos.offset_ == 0) &&
                       heuristics_->get_tsc(output->parsed_.pos, a, b))
            {
                skipping_ = false;
                PARENT_POLICY::emit(output);
            } else {
                PARENT_POLICY::discard(output);
            }
        }
    }

    void skip_to_next_valid_timing_packet()
    {
        skipping_ = true;
    }

    void set_tsc_heuristics(shared_ptr<tsc_heuristics> heuristics)
    {
        heuristics_ = heuristics;
    }

private:
    bool                       skipping_;
    shared_ptr<tsc_heuristics> heuristics_;
}; // template skip_to_next_timing_packet_on_request

} // namespace sat

#endif
