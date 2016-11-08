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
#include "sat-rtit-parser.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
using namespace std;


namespace sat {

void rtit_parser_output::tnt()
{
    unsigned int packet = parsed_.tnt.packet;
    unsigned int mask   = 0x40;

    while (!(packet & mask)) {
        mask >>= 1;
    }
    mask >>= 1;

    // TODO: need to consider skipping
    while (mask) {
        if (packet & mask) { //  T
            t();
        } else {             // NT
            nt();
        }
        mask >>= 1;
    }
}

void rtit_parser_output::stop_parsing()
{
    if (parser_) {
        parser_->stop_parsing();
    }
}

void rtit_parser_output::skip_to_psb()
{
    if (parser_) {
        parser_->skip_to_psb();
    }
}

void rtit_parser_output::set_parser(rtit_parser_imp* parser)
{
    parser_ = parser;
}

namespace {

    rtit_parser_output::token_func const fup_table[] =
    {
        &rtit_parser_output::fup_pge,
        &rtit_parser_output::fup_pgd,
        &rtit_parser_output::fup_buffer_overflow,
        &rtit_parser_output::fup_pcc,
        0, // Reserved
        0, // Reserved
        &rtit_parser_output::tip,
        &rtit_parser_output::fup_far
    };

} // namespace


rtit_parser_imp::rtit_parser_imp(shared_ptr<rtit_parser_input>  input,
                                 shared_ptr<rtit_parser_output> output)
    : rtit_parser_state(),
      cycle_accurate_(),
      stopping_(),
      input_(input),
      output_(output)
{
    pos_.offset_ = input->initial_offset();
    output_->set_parser(this);
}

void rtit_parser_imp::stop_parsing()
{
    stopping_ = true;
}

bool rtit_parser_imp::was_stopped()
{
    return stopping_;
}

void rtit_parser_imp::skip_to_psb()
{
    skipping_to_psb_ = true;
}

bool rtit_parser_imp::parse(bool cycle_accurate, bool skip_to_first_psb)
{
    bool          ok = true;
    unsigned char c;

    token_func f = 0;

    cycle_accurate_ = cycle_accurate;
    if (!skip_to_first_psb) skipped_to_first_psb_ = true;
    skipping_to_psb_ = skipped_to_first_psb_ ? skipping_to_psb_
                                             : skip_to_first_psb;

    pos_.offset_ += size_;
    size_ = 0;

    if (!stopping_) {
        if (get_next(c)) {
            if (skipping_to_psb_) {
                f = find_psb(OTHER, c);
                if (f) {
                    skipping_to_psb_      = false;
                    skipped_to_first_psb_ = true;
                }
            } else {
                f = parse_packet(c);
            }

            if (!f) {
                ok = false;
            }
        }
    }

    output_->parsed_.token = f;

    return ok && !input_->bad();
}


rtit_parser_imp::token_func rtit_parser_imp::find_psb(REASON        reason,
                                                      unsigned char c)
{
    token_func f  = 0;
    bool       ok = true;

    rtit_pos skip_start = pos_;

    if (size_) {
        pos_.offset_ += size_ - 1;
        size_ = 1;
    }

    while (ok) {
        if (c == 0xc0) {
            int zeros = 8;
            while (zeros && (ok = get_next(c))) {
                if (c != 0) {
                    pos_.offset_ += size_ - 1;
                    size_ = 1;
                    break;
                }
                --zeros;
            }

            if (zeros == 0) {
                break;
            }
        } else {
            pos_.offset_ += size_;
            size_ = 0;
            ok = get_next(c);
        }
    }

    rtit_offset skipped = pos_.offset_ - skip_start.offset_;
    if (skipped) {
        skip(reason, skip_start, skipped);
    }

    if (ok) {
         f = output_psb();
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_packet(unsigned char c)
{
    token_func f = 0;

    // TODO: optimize nesting after analyzing packet distribution
    if (c & 0x80) {
        if (c & 0x40) {
            // Extended
            f = parse_extended(c);
        } else {
            // FUP
            f = parse_fup(c);
        }
    } else if (c != 0) {
        // TNT
        f = parse_tnt(c);
    } else {
        warn(ZERO, "--- ZERO");
        f = parse_null(c);
        skip_to_psb();
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_null(unsigned char c)
{
    output_->parsed_.pos         = pos_;
    output_->parsed_.size        = size_;
    output_->parsed_.null.packet = c;
    return &rtit_parser_output::null;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_tnt(unsigned char c)
{
    output_->parsed_.pos        = pos_;
    output_->parsed_.size       = size_;
    output_->parsed_.tnt.packet = c;
    return &rtit_parser_output::tnt;
}


rtit_parser_imp::token_func rtit_parser_imp::parse_fup(unsigned char c)
{
    token_func f = 0;
    bool ok = false;

    token_func fup = fup_table[(c >> 3) & 0x7];

    if (fup) {
        bool compressed;
        ok = parse_lip(c, compressed);
        if (ok) {
            output_->parsed_.pos            = pos_;
            output_->parsed_.size           = size_;
            output_->parsed_.fup.address    = pos_.lip_;
            output_->parsed_.fup.compressed = compressed;
            f = fup;
        } else {
            if (get_next(c)) {
                return find_psb(BROKEN_LIP, c);
            }
        }
    } else {
        warn(RESERVED_PACKET, "*** Reserved FUP(%d)", (c >> 3) & 0x7);
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_extended(unsigned char c)
{
    // TODO: optimize nesting after analyzing packet distribution
    if (c & 0x20) { // Reserved: skip
        warn(RESERVED_PACKET, "*** Reserved ext with bit 5: %x", (unsigned)c);
        return 0; // TODO: is it that bad?
    } else if (c & 0x10) { // STS (Super Time Synch)
        return parse_sts(c);
    } else if (c & 0x8) { // Reserved: skip
        warn(RESERVED_PACKET, "*** Reserved ext with bit 3: %x", (unsigned)c);
        return 0; // TODO: is it that bad?
    } else if (c & 0x4) { // MTC (Mini Time Counter)
        return parse_mtc(c);
    } else if (c & 0x2) { // PIP (Paging Information Packet)
        return parse_pip(c);
    } else if (c & 0x1) { // TraceSTOP
        output_->parsed_.pos  = pos_;
        output_->parsed_.size = size_;
        return &rtit_parser_output::tracestop;
    } else { // PSB (Packet Stream Boundary)
        return parse_psb();
    }
}

rtit_parser_imp::token_func rtit_parser_imp::parse_sts(unsigned char c)
{
    token_func f = 0;
    unsigned   acbr = (c & 0xf) << 2; // Actual Core/Bus Ratio
    unsigned   ecbr;                  // Effective Core/Bus Ratio

    if (get_next(c)) {
        // get acbr & ecbr
        acbr |= (c & 0xc0) >> 6;
        ecbr  = (c & 0x3f);

        // get big time counter
        unsigned long long tsc   = 0;
        if (parse_unsigned(5, tsc)) {
            output_->parsed_.pos      = pos_;
            output_->parsed_.size     = size_;
            output_->parsed_.sts.acbr = acbr;
            output_->parsed_.sts.ecbr = ecbr;
            output_->parsed_.sts.tsc  = tsc;
            f = &rtit_parser_output::sts;
        }
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_pip(unsigned char c)
{
    token_func f      = 0;
    bool       cr0_pg = c & 0x1;

    unsigned long long cr3;
    if (parse_unsigned(5, cr3)) {
        output_->parsed_.pos        = pos_;
        output_->parsed_.size       = size_;
        output_->parsed_.pip.cr0_pg = cr0_pg;
        output_->parsed_.pip.cr3    = cr3;
        f = &rtit_parser_output::pip;
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_mtc(unsigned char c)
{
    token_func f   = 0;
    unsigned   rng = c & 0x3;

    if (get_next(c)) {
        output_->parsed_.pos     = pos_;
        output_->parsed_.size    = size_;
        output_->parsed_.mtc.rng = rng;
        output_->parsed_.mtc.tsc = c;
        f = &rtit_parser_output::mtc;
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_psb()
{
    token_func    f = 0;
    unsigned char c;
    int           zeros = 8;

    while (zeros && get_next(c)) {
        if (c != 0) {
            if (zeros == 8 && c == 0xc0) {
                warn(C0, "--- C0 bug");
                return find_psb(C0, c);
            } else {
                warn(BROKEN_PSB, "--- PSB with %d zeros missing", zeros);
                return find_psb(BROKEN_PSB, c);
            }
        }
        --zeros;
    }

    if (!zeros) {
        f = output_psb();
    }

    return f;
}

rtit_parser_imp::token_func rtit_parser_imp::parse_ccp()
{
    pos_.offset_ += size_;
    size_ = 0;

    token_func    f = 0;
    unsigned char c;
    unsigned      ccnt;

    if (get_next(c)) {
        ccnt = c & 0x3;
        unsigned cntp = (c >> 2) & 0x3f;
        unsigned shift = 6;

        while (ccnt-- > 1 && get_next(c)) {
            cntp |= ((unsigned)c) << shift;

            shift += 8;
        }

        if (ccnt == 0) {
            output_->parsed_.pos      = pos_;
            output_->parsed_.size     = size_;
            output_->parsed_.ccp.cntp = cntp;
            f = &rtit_parser_output::ccp;
        }
    }

    return f;
}

template <typename T, unsigned B>
inline T signextend(const T x)
{
        struct {T x:B;} s;
            return s.x = x;
}

bool rtit_parser_imp::parse_lip(unsigned char c, bool& compressed)
{
    unsigned cnt = c & 0x3;
    if (cnt == 0x3) {
        warn(BROKEN_LIP, "--- LIP with CNT %u", (unsigned)c & 0x3);
        return false;
    }

    bool     zext  = (c & 0x4);
    unsigned count = 2 + 2 * cnt;

    if (zext) {
        pos_.lip_ = 0;
    }

    compressed = !zext && count != 6;

    bool ok    = true;
    rva  mask  = 0xff;
    int  shift = 0;
    while (count-- && (ok = get_next(c))) {
        pos_.lip_ &= ~(mask << shift);
        pos_.lip_ |= (((rva)c) << shift);

        shift += 8;
    }

    if (ok) {
        pos_.lip_ = signextend<int64_t, 48>(pos_.lip_);
    } else {
        warn(BROKEN_LIP, "--- %u bytes missing from a LIP", count + 1);
    }

    return ok;
}

bool rtit_parser_imp::parse_unsigned(unsigned bytes, unsigned long long& value)
{
    unsigned char      c;
    bool               ok    = true;
    unsigned long long v     = 0;
    unsigned           shift = 0;

    while (bytes-- && (ok = get_next(c))) {
        v |= ((unsigned long long)c) << shift;

        shift += 8;
    }

    if (ok) {
        value = v;
    }

    return ok;
}

bool rtit_parser_imp::get_next(unsigned char& byte)
{
    bool ok = input_->get_next(byte);

    if (ok) {
        ++size_;
    }

    return ok;
}

void rtit_parser_imp::warn(REASON type, const char* fmt, ...)
{
    char* text = 0;
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&text, fmt, ap) != -1) {
        output_->warning(pos_, type, text);
        free(text);
    } else {
        output_->warning(pos_, OTHER, "!!! FAILED TO FORMAT A WARNING");
        fprintf(stderr, "!!! FAILED TO FORMAT A WARNING");
    }
    va_end(ap);
}

rtit_parser_imp::token_func rtit_parser_imp::output_psb()
{
    output_->parsed_.pos  = pos_;
    output_->parsed_.size = size_;
    return &rtit_parser_output::psb;
}

}
