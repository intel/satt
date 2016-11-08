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
#ifndef SAT_RTIT_WORKAROUNDS_H
#define SAT_RTIT_WORKAROUNDS_H

#include "sat-rtit-parser.h"

namespace sat {

    template <class PARENT_POLICY = default_rtit_policy>
    class synthesize_dropped_mtcs : public PARENT_POLICY {
    public:
        synthesize_dropped_mtcs(shared_ptr<rtit_parser_output> discard) :
            PARENT_POLICY(discard),
            packets_since_prev_mtc_(1), // don't synthesize for the first mtc
            rng_(3) // TODO: get from sideband
        {}

        void emit(rtit_parser_output* output)
        {
            rtit_parser_output::token_func f = output->parsed_.token;
            if (f == &rtit_parser_output::mtc ||
                f == &rtit_parser_output::sts)
            {
                unsigned char mtc;
                if (f ==  &rtit_parser_output::sts) {
                    mtc = (output->parsed_.sts.tsc >> (7 + 2 * rng_)) & 0xff;
                } else {
                    // f == &rtit_parser_output::mtc
                    mtc  = output->parsed_.mtc.tsc;
                    rng_ = output->parsed_.mtc.rng;

                    if (packets_since_prev_mtc_ == 0 && prev_mtc_ != mtc) {
                        for (++prev_mtc_; prev_mtc_ != mtc; ++prev_mtc_) {
                            // synthesize mtc
                            rtit_parser_output::item tmp = output->parsed_;
                            output->parsed_.mtc.tsc = prev_mtc_;
                            output->parsed_.size    = 0;

                            PARENT_POLICY::emit(output);
                            output->parsed_ = tmp;
                        }
                    }
                }

                prev_mtc_ = mtc;
                packets_since_prev_mtc_ = 0;
            } else if (f != &rtit_parser_output::psb) {
                ++packets_since_prev_mtc_;
            }

            PARENT_POLICY::emit(output);
        }

    private:
        unsigned      packets_since_prev_mtc_;
        unsigned      rng_;
        unsigned char prev_mtc_;
    };

    template <class PARENT_POLICY = default_rtit_policy>
    class postpone_early_mtc : public PARENT_POLICY {
    public:
        postpone_early_mtc(shared_ptr<rtit_parser_output> discard) :
            PARENT_POLICY(discard),
            postponing_(false)
        {}

        void emit(rtit_parser_output* output)
        {
            rtit_parser_output::token_func f = output->parsed_.token;
            if (f == &rtit_parser_output::mtc) {
                if (!postponing_) {
                    // save the mtc for later
                    postponed_  = output->parsed_;
                    postponing_ = true;
                } else {
                    // output the previous mtc and keep the new one
                    rtit_parser_output::item tmp = output->parsed_;
                    output->parsed_ = postponed_;
                    PARENT_POLICY::emit(output);
                    postponed_ = tmp;
                }
            } else {
                if (postponing_) {
                    if (f == &rtit_parser_output::sts &&
                        ((unsigned char) // DO NOT REMOVE THIS CAST!!!
                         (((output->parsed_.sts.tsc >>
                            (7 + 2 * postponed_.mtc.rng)) & 0xff) +
                          1) == postponed_.mtc.tsc))
                    {
                        // output the sts before the postponed mtc
                        output->parsed_.pos.offset_ = postponed_.pos.offset_;
                        PARENT_POLICY::emit(output);
                        postponed_.pos.offset_ += output->parsed_.size;
                        output->parsed_ = postponed_;
                        PARENT_POLICY::emit(output);
                    } else {
                        // output the mtc before the other token
                        rtit_parser_output::item tmp = output->parsed_;
                        output->parsed_ = postponed_;
                        PARENT_POLICY::emit(output);
                        output->parsed_ = tmp;
                        PARENT_POLICY::emit(output);
                    }
                    postponing_ = false;
                } else {
                    PARENT_POLICY::emit(output);
                }
            }
        }

    private:
        bool                     postponing_;
        rtit_parser_output::item postponed_;
    };

    class skip_after_overflow_with_compressed_lip : public default_rtit_policy {
    public:
        skip_after_overflow_with_compressed_lip(
            shared_ptr<rtit_parser_output> discard) :
            default_rtit_policy(discard),
            discard_(discard),
            skipping_(false)
        {}

        void emit(rtit_parser_output* output)
        {
            bool ok_to_output = true;
            rtit_parser_output::token_func f = output->parsed_.token;

            if (!skipping_) {
                // normal operation;
                // see if we have an overflow with a compressed address
                if (f == &rtit_parser_output::fup_buffer_overflow &&
                    output->parsed_.fup.compressed)
                {
                    // must skip until we find a non-compressed address
                    skipping_    = true;
                    ok_to_output = false;
                    output->warning(output->parsed_.pos,
                                    BROKEN_OVERFLOW,
                                    "*** OVERFLOW with compressed LIP");
                }
            } else {
                // we are skipping due to an overflow with a compressed address;
                // must not let any TNT's or compressed LIP's through
                if (f == &rtit_parser_output::tnt) {
                    ok_to_output = false;
                } else if (f == &rtit_parser_output::fup_pge             ||
                           f == &rtit_parser_output::fup_pgd             ||
                           f == &rtit_parser_output::fup_buffer_overflow ||
                           f == &rtit_parser_output::fup_pcc             ||
                           f == &rtit_parser_output::tip                 ||
                           f == &rtit_parser_output::fup_far)
                {
                    if (output->parsed_.fup.compressed) {
                        ok_to_output = false;
                    } else {
                        // got a FUP with a proper address; stop skipping
                        skipping_ = false;
                    }
                } else if (f == &rtit_parser_output::psb) {
                    // got a PSB; stop skipping
                    skipping_ = false;
                } else {
                    // let all other packets go through
                }
            }

            if (ok_to_output) {
                ((*output).*f)();
            } else if (discard_) {
                ((*discard_).*f)();
                // TODO: call output->skip()?
            }
        }

        void discard(rtit_parser_output* output)
        {
            if (discard_) {
                ((*discard_).*output->parsed_.token)();
            }
        }

    private:
        shared_ptr<rtit_parser_output> discard_;
        bool                           skipping_;
    };

} // namespace sat

#endif // SAT_RTIT_WORKAROUNDS_H
