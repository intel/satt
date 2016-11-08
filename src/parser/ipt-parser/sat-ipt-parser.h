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
#ifndef SAT_IPT_PARSER_H
#define SAT_IPT_PARSER_H

#include <cstdint>
#include <string>
#include <algorithm>

namespace sat {

using namespace std;

// default input policy is to always fail
class ipt_parser_dummy_input {
public:
    bool get_next(uint8_t& c) { return false; }
    bool get_next(size_t n, uint8_t c[]) { return false; }
    bool bad() { return true; }
    void mark_beginning_of_packet() {}
    bool is_fast_forwarding() { return true; }
}; // ipt_parser_dummy_input

using ipt_exec_mode = enum {
    IPT_EXEC_MODE_16 = 0x00, IPT_EXEC_MODE_64 = 0x01, IPT_EXEC_MODE_32 = 0x02
};

using ipt_tsx_type = enum {
    IPT_TSX_OUT = 0x00, IPT_TSX_IN = 0x01, IPT_TSX_ABORT = 0x02
};

enum REASON {
    ZERO,
    BROKEN_OVERFLOW,
    RESERVED_PACKET,
    C0,
    BROKEN_PSB,
    BROKEN_LIP,
    OTHER,
};

template <class OUTPUT>
struct ipt_parser_token {
    decltype(&OUTPUT::eof) func;
    union {
        struct {
            uint64_t bits;
            uint64_t mask;
        }             tnt;
        uint64_t      tip;
        uint8_t       cbr;
        uint64_t      tsc;
        uint8_t       ctc;
        struct {
            uint64_t cr3;
            bool     nr;
        }             pip;
        ipt_exec_mode mode;
        ipt_tsx_type  tsx;
        struct {
            uint16_t ctc;
            uint16_t fast;
        }             tma;
        uint64_t      vmcs_base_address;
    };
    uint64_t               last_ip;
}; // ipt_parser_token

// default output policy is to not output anything
template <class INPUT>
class ipt_parser_dummy_output {
public:
    using token = ipt_parser_token<ipt_parser_dummy_output>;

    ipt_parser_dummy_output(const INPUT&) {}

    void output_packet(const uint8_t packet[]) {}

    void short_tnt(token&) {}
    void long_tnt (token&) {}
    void tip      (token&) {}
    void tip_pge  (token&) {}
    void tip_pgd  (token&) {}
    void fup      (token&) {}
    void pip      (token&) {}
    void tsc      (token&) {}
    void mtc      (token&) {}
    void mode_exec(token&) {}
    void mode_tsx (token&) {}
    void tracestop(token&) {}
    void cbr      (token&) {}
    void tma      (token&) {}
    void cyc      (token&) {}
    void vmcs     (token&) {}
    void ovf      (token&) {}
    void psb      (token&) {}
    void psbend   (token&) {}
    void mnt      (token&) {}
    void pad      (token&) {}
    void eof      (token&) {}

    void report_warning(const string& message) {}
    void report_error(const string& message) {}
    void set_ff_state(bool state) {}
}; // ipt_parser_dummy_output

// base class for non-virtual output methods
template <class DERIVED>
class ipt_parser_output_base {
public:
    using token = ipt_parser_token<DERIVED>;

    void output_packet(const uint8_t packet[]) {}

    void short_tnt(token&) {}
    void long_tnt (token&) {}
    void tip      (token&) {}
    void tip_pge  (token&) {}
    void tip_pgd  (token&) {}
    void fup      (token&) {}
    void pip      (token&) {}
    void tsc      (token&) {}
    void mtc      (token&) {}
    void mode_exec(token&) {}
    void mode_tsx (token&) {}
    void tracestop(token&) {}
    void cbr      (token&) {}
    void tma      (token&) {}
    void cyc      (token&) {}
    void vmcs     (token&) {}
    void ovf      (token&) {}
    void psb      (token&) {}
    void psbend   (token&) {}
    void mnt      (token&) {}
    void pad      (token&) {}
    void eof      (token&) {}

    void report_warning(const string& message) {}
    void report_error(const string& message) {}
    void set_ff_state(bool state) {}
}; // ipt_parser_output_base

#if 0
// base class for virtual (and hence slower) output methods
template <class DERIVED>
class ipt_parser_virtual_output {
public:
    using token = ipt_parser_token<DERIVED>;

    virtual void output_packet(const uint8_t packet[]) {}

    virtual void short_tnt(token&) {}
    virtual void long_tnt (token&) {}
    virtual void tip      (token&) {}
    virtual void tip_pge  (token&) {}
    virtual void tip_pgd  (token&) {}
    virtual void fup      (token&) {}
    virtual void pip      (token&) {}
    virtual void tsc      (token&) {}
    virtual void mtc      (token&) {}
    virtual void mode_exec(token&) {}
    virtual void mode_tsx (token&) {}
    virtual void tracestop(token&) {}
    virtual void cbr      (token&) {}
    virtual void tma      (token&) {}
    virtual void cyc      (token&) {}
    virtual void vmcs     (token&) {}
    virtual void ovf      (token&) {}
    virtual void psb      (token&) {}
    virtual void psbend   (token&) {}
    virtual void mnt      (token&) {}
    virtual void pad      (token&) {}
    virtual void eof      (token&) {}

    virtual void report_warning(const string& message) {}
    virtual void report_error(const string& message) {}
}; // ipt_parser_virtual_output
#endif

// base class for error messaging
class ipt_parser_base_output_policy {
public:
    virtual void report_warning(const string& message) = 0;
    virtual void report_error(const string& message) = 0;
}; // ipt_parser_dummy_error_output

// default output policy is to call the methods of the output class
template <class INPUT, class OUTPUT, class SCANNER, class EVALUATOR>
class ipt_call_output_method : public ipt_parser_base_output_policy
{
public:
    ipt_call_output_method(const INPUT& input, OUTPUT& output) :
        input_(input), output_(output)
    {}

    using lexeme_func = decltype(&EVALUATOR::eof);
    using token_func = decltype(&OUTPUT::eof);

    void output_lexeme(lexeme_func lexeme, uint8_t packet[])
    {
        output_.output_packet(packet);
    }

    void output_token(ipt_parser_token<OUTPUT>& token)
    {
        (output_.*token.func)(token);
    }

    void report_warning(const string& message) override
    {
        output_.report_warning(message);
    }

    void report_error(const string& message) override
    {
        output_.report_error(message);
    }

protected:
    const INPUT& input_;
    OUTPUT&      output_;
}; // ipt_call_output_method

template <class INPUT, class OUTPUT>
class ipt_scanner
{
public:
    using token = decltype(&OUTPUT::eof);

    ipt_scanner(INPUT& input, ipt_parser_base_output_policy& output_policy) :
        input_(input),
        output_policy_(&output_policy)
    {}

    token scan(uint8_t packet[])
    {
        token t;

        switch (packet[0]) {
        case 0x00: // 00000000
            t = &OUTPUT::pad;
            break;
        case 0x02: // 00000010
            t = parse_extended(packet);
            break;
        case 0x19: // 00011001
            t = read_packet(&OUTPUT::tsc, packet, 1, 7);
            break;
        case 0x59: // 01011001
            t = read_packet(&OUTPUT::mtc, packet, 1, 1);
            break;
        case 0x99: // 10011001
            t = parse_mode(packet);
            break;
        default:
            if ((packet[0] & 0x01) == 0x00) {        // xxxxxxx0
                t = &OUTPUT::short_tnt;
            } else if ((packet[0] & 0x03) == 0x03) { // xxxxxx11
                t = parse_cyc(packet);
            } else if ((packet[0] & 0x1f) == 0x01) { // xxx00001
                t = read_tip_packet(&OUTPUT::tip_pgd, packet);
            } else if ((packet[0] & 0x1f) == 0x0d) { // xxx01101
                t = read_tip_packet(&OUTPUT::tip, packet);
            } else if ((packet[0] & 0x1f) == 0x11) { // xxx10001
                t = read_tip_packet(&OUTPUT::tip_pge, packet);
            } else if ((packet[0] & 0x1f) == 0x1d) { // xxx11101
                t = read_tip_packet(&OUTPUT::fup, packet);
            } else {
                output_policy_->report_error("unknown packet");
                t = 0;
            }
        }

        return t;
    }

private:
    bool get_next(uint8_t& c)
    {
        return input_.get_next(c);
    }

    bool get_next(unsigned from, unsigned to, uint8_t c[])
    {
        return input_.get_next(to - from + 1, &c[from]);
    }

    token read_packet(token t, uint8_t packet[], unsigned from, unsigned to)
    {
        if (input_.get_next(to - from + 1, &packet[from])) {
            return t;
        } else {
            output_policy_->report_warning("unexpected EOF");
            return &OUTPUT::eof;
        }
    }

    token parse_extended(uint8_t packet[])
    {
        token t;

        if (get_next(packet[1])) {
            switch (packet[1]) {
                case 0x03: // 00000011
                    t = read_packet(&OUTPUT::cbr, packet, 2, 3);
                    break;
                case 0x23: // 00100011
                    t = &OUTPUT::psbend;
                    break;
                case 0x43: // 01000011
                    t = read_packet(&OUTPUT::pip, packet, 2, 7);
                    break;
                case 0x73: // 01110011
                    t = read_packet(&OUTPUT::tma, packet, 2, 6);
                    break;
                case 0x82: // 10000010
                    t = parse_psb(packet);
                    break;
                case 0x83: // 10000011
                    t = &OUTPUT::tracestop;
                    break;
                case 0xa3: // 10100011
                    t = read_packet(&OUTPUT::long_tnt, packet, 2, 7);
                    break;
                case 0xc3: // 11000011
                    t = read_packet(&OUTPUT::mnt, packet, 2, 9);
                    break;
                case 0xc8: // 11001000
                    t = read_packet(&OUTPUT::vmcs, packet, 2, 6);
                    break;
                case 0xf3: // 11110011
                    t = &OUTPUT::ovf;
                    break;
                default:
                    output_policy_->report_error("broken extended package");
                    t = 0;
            }
        } else {
            output_policy_->report_warning("unexpected EOF in extended package");
            t = &OUTPUT::eof;
        }

        return t;
    }

    token parse_mode(uint8_t packet[])
    {
        if (get_next(packet[1])) {
            auto leaf_id = packet[1] >> 5;
            switch (leaf_id) {
            case 0x00:
                return &OUTPUT::mode_exec;
            case 0x01:
                return &OUTPUT::mode_tsx;
            default:
                output_policy_->report_error("unknown MODE");
                return 0;
            }
        } else {
            output_policy_->report_warning("unexpected EOF in mode package");
            return &OUTPUT::eof;
        }
    }

    token read_tip_packet(token t, uint8_t packet[])
    {
        unsigned ip_bytes = packet[0] >> 5;

        if (ip_bytes > 3) {
            char tmp[50];
            sprintf(tmp, "TIP IPBytes value too high (%d) packet[0] = 0x%x", ip_bytes, packet[0]);
            output_policy_->report_error(tmp);
            return 0;
        } else {
            return read_packet(t, packet, 1, 2 * ip_bytes);
        }
    }

    token parse_cyc(uint8_t packet[])
    {
        int i = 0;
        bool exp = packet[0] & 0x04;
        while (exp) {
            ++i;
            if (i > 14) {
                output_policy_->report_error("CYC package is too long");
                return 0;
            }
            if (!get_next(packet[i])) {
                output_policy_->report_warning("unexpected EOF in CYC");
                return &OUTPUT::eof;
            }
            exp = packet[i] & 0x01;
        }

        return &OUTPUT::cyc;
    }

    token parse_psb(uint8_t packet[])
    {
        token t;

        if (get_next(packet[ 2]) && packet[ 2] == 0x02 &&
            get_next(packet[ 3]) && packet[ 3] == 0x82 &&
            get_next(packet[ 4]) && packet[ 4] == 0x02 &&
            get_next(packet[ 5]) && packet[ 5] == 0x82 &&
            get_next(packet[ 6]) && packet[ 6] == 0x02 &&
            get_next(packet[ 7]) && packet[ 7] == 0x82 &&
            get_next(packet[ 8]) && packet[ 8] == 0x02 &&
            get_next(packet[ 9]) && packet[ 9] == 0x82 &&
            get_next(packet[10]) && packet[10] == 0x02 &&
            get_next(packet[11]) && packet[11] == 0x82 &&
            get_next(packet[12]) && packet[12] == 0x02 &&
            get_next(packet[13]) && packet[13] == 0x82 &&
            get_next(packet[14]) && packet[14] == 0x02 &&
            get_next(packet[15]) && packet[15] == 0x82)
        {
            t = &OUTPUT::psb;
        } else {
            output_policy_->report_error("broken PSB");
            t = 0;
        }

        return t;
    }

    INPUT&                         input_;
    ipt_parser_base_output_policy* output_policy_;
}; // ipt_scanner

template <class OUTPUT>
class ipt_evaluator
{
public:
    using token = ipt_parser_token<OUTPUT>;
    using lexeme = token& (ipt_evaluator::*)(const uint8_t packet[]);

    // TODO: also grab a reference to input to be able to determine packet size
    ipt_evaluator(ipt_parser_base_output_policy& output_policy) :
        output_policy_(&output_policy)
    {}

    token& evaluate(lexeme l, const uint8_t packet[])
    {
        return (this->*l)(packet);
    }

    token& short_tnt(const uint8_t packet[])
    {
        value.tnt.bits = packet[0] >> 1;
        value.tnt.mask = 0x40;
        while (!(value.tnt.bits & value.tnt.mask)) {
            value.tnt.mask >>= 1;
        }
        value.tnt.mask >>= 1;
        value.func = &OUTPUT::short_tnt;
        return value;
    }

    token& long_tnt(const uint8_t packet[]) {
#if 0 // portable version
        value.tnt.bits =  (uint64_t)packet[2]        |
                         ((uint64_t)packet[3] <<  8) |
                         ((uint64_t)packet[4] << 16) |
                         ((uint64_t)packet[5] << 24) |
                         ((uint64_t)packet[6] << 32) |
                         ((uint64_t)packet[7] << 40);
#else // little-endian version with non-aligned access
        value.tnt.bits = (*(uint64_t*)packet) >> 16;
#endif
        value.tnt.mask = 1UL << 47;
        while (!(value.tnt.bits & value.tnt.mask)) {
            value.tnt.mask >>= 1;
        }
        value.tnt.mask >>= 1;
        value.func = &OUTPUT::long_tnt;
        return value;
    }

    token& tip(const uint8_t packet[]) {
        evaluate_lip(packet);
        value.func = &OUTPUT::tip;
        return value;
    }

    token& tip_pge(const uint8_t packet[])
    {
        evaluate_lip(packet);
        value.func = &OUTPUT::tip_pge;
        return value;
    }

    token& tip_pgd(const uint8_t packet[])
    {
        evaluate_lip(packet);
        value.func = &OUTPUT::tip_pgd;
        return value;
    }

    token& fup(const uint8_t packet[])
    {
        evaluate_lip(packet);
        value.func = &OUTPUT::fup;
        return value;
    }

    token& pip(const uint8_t packet[])
    {
#if 0 // portable version
        value.pip.nr  = (uint64_t)packet[2] & 0x01;
        value.pip.cr3 = (((uint64_t)packet[2] <<  4) |
                         ((uint64_t)packet[3] << 12) |
                         ((uint64_t)packet[4] << 20) |
                         ((uint64_t)packet[5] << 28) |
                         ((uint64_t)packet[6] << 36) |
                         ((uint64_t)packet[7] << 44)) &
                        0x1fffffffffe0;
#else // little-endian version with non-aligned access
        value.pip.cr3 = *(uint64_t*)packet;
        value.pip.nr  = value.pip.cr3 & 0x10000;
        value.pip.cr3 >>= 17;
        value.pip.cr3 <<=  5;
#endif
        value.func = &OUTPUT::pip;
        return value;
    }

    token& tsc(const uint8_t packet[])
    {
#if 0 // portable version
        value.tsc  = (uint64_t)packet[1]        |
                    ((uint64_t)packet[2] <<  8) |
                    ((uint64_t)packet[3] << 16) |
                    ((uint64_t)packet[4] << 24) |
                    ((uint64_t)packet[5] << 32) |
                    ((uint64_t)packet[6] << 40) |
                    ((uint64_t)packet[7] << 48);
#else // little-endian version with non-aligned access
        value.tsc  = (*(uint64_t*)packet) >> 8;
#endif
        value.func = &OUTPUT::tsc;
        return value;
    }

    token& mtc(const uint8_t packet[])
    {
        value.ctc  = packet[1];
        value.func = &OUTPUT::mtc;
        return value;
    }

    token& mode_exec(const uint8_t packet[])
    {
        auto mode = packet[1] & 0x03;
        if (mode <= 2) {
            value.mode = static_cast<ipt_exec_mode>(mode);
            value.func = &OUTPUT::mode_exec;
        } else {
            output_policy_->report_error("illegal exec mode");
            value.func = 0;
        }
        return value;
    }

    token& mode_tsx(const uint8_t packet[])
    {
        auto tx = packet[1] & 0x03;
        if (tx <= 2) {
            value.tsx = static_cast<ipt_tsx_type>(tx);
            value.func = &OUTPUT::mode_tsx;
        } else {
            output_policy_->report_error("illegal transaction mode");
            value.func = 0;
        }
        return value;
    }

    token& tracestop(const uint8_t packet[])
    {
        value.func = &OUTPUT::tracestop;
        return value;
    }

    token& cbr(const uint8_t packet[])
    {
        value.cbr  = packet[2];
        value.func = &OUTPUT::cbr;
        return value;
    }

    token& tma(const uint8_t packet[])
    {
#if 0 // portable version
        value.tma.ctc  =  ((uint16_t)packet[3] << 8) | packet[2];
        value.tma.fast = (((uint16_t)packet[6] << 8) | packet[5]) & 0x01ff;
#else // little-endian version with non-aligned access
        value.tma.ctc  =  *(uint16_t*)&packet[2];
        value.tma.fast = (*(uint16_t*)&packet[5]) & 0x01ff;
#endif
        value.func     = &OUTPUT::tma;
        return value;
    }

    token& cyc(const uint8_t packet[])
    {
        // don't bother with cycle counter value since we are not using it
        value.func = &OUTPUT::cyc;
        return value;
    }

    token& vmcs(const uint8_t packet[])
    {
        value.vmcs_base_address = ((uint64_t)packet[6] << 44) |
                                  ((uint64_t)packet[5] << 36) |
                                  ((uint64_t)packet[4] << 28) |
                                  ((uint64_t)packet[3] << 20) |
                                  ((uint64_t)packet[2] << 12);
        value.func              = &OUTPUT::vmcs;
        return value;
    }

    token& ovf(const uint8_t packet[])
    {
        value.func = &OUTPUT::ovf;
        return value;
    }

    token& psb(const uint8_t packet[])
    {
        value.func = &OUTPUT::psb;
        return value;
    }

    token& psbend(const uint8_t packet[])
    {
        value.func = &OUTPUT::psbend;
        return value;
    }

    token& mnt(const uint8_t packet[])
    {
        // don't bother with the payload since we are not using it
        value.func = &OUTPUT::mnt;
        return value;
    }

    token& pad(const uint8_t packet[])
    {
        value.func = &OUTPUT::pad;
        return value;
    }

    token& eof(const uint8_t packet[])
    {
        value.func = &OUTPUT::eof;
        return value;
    }

private:
    template <typename T, unsigned B>
    inline T signextend(const T x)
    {
        struct {T x:B;} s;
        return s.x = x;
    }

    void evaluate_lip(const uint8_t packet[])
    {
        unsigned ip_bytes = packet[0] >> 5;

        switch (ip_bytes) {
        case 0x00: // IP is out of context
            value.tip = 0;
            break;
        case 0x01:
            value.tip = value.last_ip & 0xffffffffffff0000;
            value.tip |= ((uint64_t)packet[2] << 8) | packet[1];
            value.last_ip = value.tip;
            break;
        case 0x02:
            value.tip = value.last_ip & 0xffffffff00000000;
            value.tip |= ((uint64_t)packet[4] << 24) |
                         ((uint64_t)packet[3] << 16) |
                         ((uint64_t)packet[2] <<  8) |
                                    packet[1];
            value.last_ip = value.tip;
            break;
        case 0x03:
            value.tip =
                        ((uint64_t)packet[6] << 40) |
                        ((uint64_t)packet[5] << 32) |
                        ((uint64_t)packet[4] << 24) |
                        ((uint64_t)packet[3] << 16) |
                        ((uint64_t)packet[2] <<  8) |
                                   packet[1];
            value.tip = signextend<int64_t, 48>(value.tip);
            value.last_ip = value.tip;
            break;
        default:
            // this has already been taken care of by the scanner
            break;
        }
    }

    ipt_parser_base_output_policy* output_policy_;
    token                          value;
}; // ipt_evaluator

template < class INPUT                                               = ipt_parser_dummy_input,
           template <class> class OUTPUT                             = ipt_parser_dummy_output,
           template <class, class, class, class> class OUTPUT_POLICY = ipt_call_output_method,
           template <class, class> class SCANNER                     = ipt_scanner,
           template <class> class EVALUATOR                          = ipt_evaluator
         >
class ipt_parser
{
    using output_t = OUTPUT<INPUT>;
    using evaluator = EVALUATOR<output_t>;
    using scanner = SCANNER<INPUT, evaluator>;
    using output_policy = OUTPUT_POLICY<INPUT, output_t, scanner, evaluator>;

public:
    ipt_parser() :
        input_(),
        output_(input_),
        output_policy_(input_, output_),
        scanner_(input_, output_policy_),
        evaluator_(output_policy_),
        at_eof_(),
        ff_state_(false)
    {}

    INPUT&         input()  { return input_; }
    output_t&      output() { return output_; }
    output_policy& policy() { return output_policy_; }

    bool parse()
    {
        bool ok = true;

        uint8_t packet[16];
        input_.mark_beginning_of_packet();
        if (input_.get_next(packet[0])) {
            check_ff_state();
            auto lexeme = scanner_.scan(packet);

            if (lexeme) {
                output_policy_.output_lexeme(lexeme, packet);
                auto token = evaluator_.evaluate(lexeme, packet);
                if (token.func) {
                    output_policy_.output_token(token);
                } else {
                    ok = false;
                }
            } else {
                ok = false;
            }
        } else {
            if (!input_.bad() && !at_eof_) {
                // TODO: call output_lexeme as well?
                static ipt_parser_token<output_t> eof_token{&output_t::eof};
                output_policy_.output_token(eof_token);
                at_eof_ = true;
            } else {
                ok = false;
            }
        }

        return ok;
    }

private:
    void check_ff_state()
    {
        bool state = input_.is_fast_forwarding();
        if (state != ff_state_) {
            //printf("#check_ff_state; %d (%d)\n", state, ff_state_);
            ff_state_ = state;
            output_.set_ff_state(state);
        }
    }

    INPUT                input_;
    output_t             output_;
    output_policy        output_policy_;
    scanner              scanner_;
    evaluator            evaluator_;
    bool                 at_eof_;
    bool                 ff_state_;
    static constexpr int max_packet_size_ = 16; // TODO: use
}; // ipt_parser

} // namespace sat

#endif // SAT_IPT_PARSER_H
