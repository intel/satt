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
#ifndef SAT_RTIT_PKT_CNT
#define SAT_RTIT_PKT_CNT

#include "sat-rtit-parser.h"

namespace sat {

template <class PARENT_POLICY>
class last_psb : public PARENT_POLICY {
public:
    last_psb(shared_ptr<rtit_parser_output> discard) :
        PARENT_POLICY(discard),
        last_psb_offset_(),
        skipped_c0s_since_psb_()
    {
        set_pkt_mask(2); // kernel module default value
    }

    void emit(rtit_parser_output* output)
    {
        rtit_parser_output::token_func f = output->parsed_.token;
#ifdef SAT_EACH_PSB_RESETS_PKT_CNT
        if (f == &rtit_parser_output::psb)
#else // only reset PKT_CNT if the PSB was sent out due to PKT_MASK
      // TODO: instead of using a magic value of, e.g. 0x1ff0, get PKT_MASK
        if (f == &rtit_parser_output::psb &&
            (output->parsed_.pos.offset_ -
             last_psb_offset_ -
             skipped_c0s_since_psb_ >= psb_offset_limit_))
#endif
        {
            last_psb_offset_       = output->parsed_.pos.offset_;
            skipped_c0s_since_psb_ = 0;
        }
        PARENT_POLICY::emit(output);
    }

    void skip(REASON              reason,
              rtit_parser_output* output,
              rtit_pos            pos,
              rtit_offset         count)
    {
        if (reason == C0) {
            // We skipped because there were C0s in the RTIT.
            // Normally, this happens when the SAT kernel module
            // has been configured with the "buffer hack" that
            // outputs concatenated, but only partially filled
            // RTIT buffers. The unfilled gaps in the RTIT are C0s.
            // We have to take special measures to not mess up
            // pkt_cnt when we skip over the C0s.
            skipped_c0s_since_psb_ += count;
        }
        PARENT_POLICY::skip(reason, output, pos, count);
    }

    void set_pkt_mask(uint32_t mask)
    {
        psb_offset_limit_ = (0x800 << mask) - 0x10;
    }
    rtit_offset last_psb_offset() { return last_psb_offset_; }

protected:
    uint32_t    psb_offset_limit_;
    rtit_offset last_psb_offset_;
    rtit_offset skipped_c0s_since_psb_;
};


template <class PARENT_POLICY>
class pkt_cnt : public last_psb<PARENT_POLICY> {
public:
    pkt_cnt(shared_ptr<rtit_parser_output> discard) :
        last_psb<PARENT_POLICY>(discard)
    {}

    void emit(rtit_parser_output* output)
    {
        offset_ = output->parsed_.pos.offset_;
        last_psb<PARENT_POLICY>::emit(output);
    }

    rtit_offset rtit_pkt_cnt()
    {
        return offset_ -
               last_psb<PARENT_POLICY>::last_psb_offset_ -
               last_psb<PARENT_POLICY>::skipped_c0s_since_psb_;
    }

private:
    rtit_offset offset_;
};

} // namespace sat

#endif
