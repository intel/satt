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
#ifndef SAT_RTIT_PARSER_H
#define SAT_RTIT_PARSER_H

#include "sat-types.h"
#include <memory>
#include <cstdint>


namespace sat {

    using namespace std;

    typedef uint64_t rtit_offset;
    const rtit_offset rtit_offset_max = UINT64_MAX;

    struct rtit_pos {
        rtit_offset offset_;
        rva         lip_;
    };

    inline bool operator<(const rtit_pos& a, const rtit_pos& b)
    {
        return a.offset_ < b.offset_;
    }

    inline bool operator>(const rtit_pos& a, const rtit_pos& b)
    {
        return a.offset_ > b.offset_;
    }

    inline bool operator<=(const rtit_pos& a, const rtit_pos& b)
    {
        return a.offset_ <= b.offset_;
    }

    inline bool operator>=(const rtit_pos& a, const rtit_pos& b)
    {
        return a.offset_ >= b.offset_;
    }

    // reason for warning or skipping to psb
    // TODO: we are stomping on the namespace here a bit
    enum REASON {
        ZERO,
        BROKEN_OVERFLOW,
        RESERVED_PACKET,
        C0,
        BROKEN_PSB,
        BROKEN_LIP,
        OTHER,
    };

    class rtit_parser_input {
    public:
        virtual bool get_next(unsigned char& byte) = 0;
        virtual bool bad() = 0;
        virtual rtit_offset initial_offset() { return 0; }
    };

    class rtit_parser_output {
    public:
        virtual void null()                {};
        // TNT
        void tnt();
        virtual void t()                   {};
        virtual void nt()                  {};
        // FUP
        virtual void fup_pge()             {};
        virtual void fup_pgd()             {};
        virtual void fup_buffer_overflow() {};
        virtual void fup_pcc()             {};
        virtual void tip()                 {};
        virtual void fup_far()             {};
        // Extended
        virtual void sts()                 {};
        virtual void mtc()                 {};
        virtual void pip()                 {};
        virtual void tracestop()           {};
        virtual void psb()                 {};
        // Cycle Count
        virtual void ccp()                 {};

        virtual void skip(rtit_pos pos, rtit_offset count) {};
        virtual void warning(rtit_pos    pos,
                             REASON      type,
                             const char* text) {};

        void stop_parsing();

        typedef void (rtit_parser_output::* token_func)();

        typedef struct {
            token_func token;
            union {
                struct {
                    unsigned packet;
                } null;
                struct {
                    unsigned packet;
                } tnt;
                struct {
                    rva      address;
                    bool     compressed;
                } fup;
                struct {
                    unsigned acbr;
                    unsigned ecbr;
                    uint64_t tsc;
                } sts;
                struct {
                    unsigned rng;
                    unsigned tsc;
                } mtc;
                struct {
                    bool     cr0_pg;
                    uint64_t cr3;
                } pip;
                struct {
                    unsigned cntp;
                } ccp;
            };
            rtit_pos pos;
            unsigned size;
        } item;

        item parsed_;

    protected:
        void skip_to_psb();

    private:
        friend class                  rtit_parser_imp;
        template <class> friend class rtit_parser;
        void set_parser(class rtit_parser_imp* parser);

        class rtit_parser_imp* parser_; // TODO: use a smart pointer
    };

    class rtit_parser_state {
    public:
        rtit_pos pos_;
        unsigned size_;
        bool     skipping_to_psb_;
        bool     skipped_to_first_psb_;
    };

    class rtit_parser_imp : protected rtit_parser_state {
    public:
        typedef rtit_parser_output::token_func token_func;

        rtit_parser_imp(shared_ptr<rtit_parser_input>  input,
                        shared_ptr<rtit_parser_output> output);

        void stop_parsing();
        bool was_stopped();
        void skip_to_psb();

    protected:
        bool parse(bool cycle_accurate, bool skip_to_first_psb);
        bool get_next(unsigned char& byte);
        token_func find_psb(REASON reason, unsigned char c);
        token_func parse_packet(unsigned char c);
        virtual void skip(REASON      reason,
                          rtit_pos    pos,
                          rtit_offset count) = 0;

        bool                           cycle_accurate_;
        bool                           stopping_;

    private:
        token_func parse_null(unsigned char c);
        token_func parse_tnt(unsigned char c);
        token_func parse_fup(unsigned char c);
        token_func parse_extended(unsigned char c);

        token_func parse_sts(unsigned char c);
        token_func parse_pip(unsigned char c);
        token_func parse_mtc(unsigned char c);
        token_func parse_psb();

        token_func parse_ccp();

        bool parse_lip(unsigned char c, bool& compressed);
        bool parse_unsigned(unsigned bytes, unsigned long long& value);

        void warn(REASON type, const char* fmt, ...);
        token_func output_psb();

    protected:
        shared_ptr<rtit_parser_input>  input_;
        shared_ptr<rtit_parser_output> output_;
    private:
    };

    class rtit_parser_base {
    public:
        virtual bool parse(bool cycle_accurate, bool skip_to_first_psb) = 0;
    };

    class default_rtit_policy {
    public:
        default_rtit_policy(shared_ptr<rtit_parser_output> discard) {}
        static void emit(rtit_parser_output* output)
        {
            ((*output).*output->parsed_.token)();
        }
        static void discard (rtit_parser_output* output)
        {}
        static void skip(REASON              reason,
                         rtit_parser_output* output,
                         rtit_pos            pos,
                         rtit_offset         count)
        {
            output->skip(pos, count);
        }
    };

    template <class POLICY = default_rtit_policy>
    class rtit_parser : public rtit_parser_imp, public rtit_parser_base {
    public:
        rtit_parser(shared_ptr<rtit_parser_input>  input,
                    shared_ptr<rtit_parser_output> output,
                    shared_ptr<rtit_parser_output> discard = 0,
                    rva                            lip     = 0) :
            rtit_parser_imp(input, output),
            policy(discard)
        {
            if (discard) {
                discard->set_parser(this);
            }
            pos_.lip_ = lip;
        }

#if 0
        void set_lip(rva lip)
        {
            lip_ = lip;
        }
#endif

        bool parse(bool cycle_accurate = false, bool skip_to_first_psb = false)
        {
            bool       ok = true;

            stopping_ = false;
            while (!stopping_) {
                ok = rtit_parser_imp::parse(cycle_accurate, skip_to_first_psb);
                rtit_parser_output* output = output_.get();
                if (!output->parsed_.token) {
                    break;
                }

                policy.emit(output);
            }

            return ok;
        }

        POLICY policy;

    protected:
        void skip(REASON reason, rtit_pos pos, rtit_offset count)
        {
            policy.skip(reason, output_.get(), pos, count);
        }
    }; // class rtit_parser


    template <class OUTPUT_CLASS>
    class route_emit : public default_rtit_policy {
    public:
        route_emit(shared_ptr<rtit_parser_output> discard) :
            default_rtit_policy(discard)
        {}

        void route_emit_to(OUTPUT_CLASS* output)
        {
            output_ = output;
        }

        void emit(rtit_parser_output* output)
        {
            if (output_) {
                output_->emit(output);
            }
        }

        void discard(rtit_parser_output* output)
        {}

    private:
        OUTPUT_CLASS* output_;
    }; // class route_emit


    template <class PARENT_POLICY = default_rtit_policy>
    class skip_to_offset_on_request : public PARENT_POLICY {
    public:
        skip_to_offset_on_request(shared_ptr<rtit_parser_output> discard) :
            PARENT_POLICY(discard),
            skipping_(false),
            pos_()
        {}

        void emit(rtit_parser_output* output)
        {
            if (!skipping_ || output->parsed_.pos.offset_ >= pos_.offset_) {
                skipping_ = false;
                PARENT_POLICY::emit(output);
            } else {
                PARENT_POLICY::discard(output);
            }
        }

        void skip_to_position(rtit_pos pos)
        {
            pos_      = pos;
            skipping_ = true;
        }

    private:
        bool     skipping_;
        rtit_pos pos_;
    };


    class rtit_seekable_parser_input : public rtit_parser_input {
    public:
        virtual void tell(rtit_pos& pos) = 0;
        virtual bool seek(const rtit_pos& pos) = 0;
    };

    class rtit_rewindable_parser_base {
    public:
        virtual void mark() = 0;
        virtual bool rewind() = 0;
    };

    template <class POLICY = default_rtit_policy>
    class rtit_rewindable_parser :
        public rtit_rewindable_parser_base,
        public rtit_parser<POLICY>
    {
    public:
        rtit_rewindable_parser(
            shared_ptr<rtit_seekable_parser_input> input,
            shared_ptr<rtit_parser_output>         output,
            shared_ptr<rtit_parser_output>         discard = 0)
            :
            rtit_parser<POLICY>(input, output, discard),
            input_(input)
        {
            rtit_parser<POLICY>::policy.set_parser(this);
        }

        void mark()
        {
            marked_parser_state_ = *this;
            input_->tell(marked_input_position_);
        }

        bool rewind()
        {
            *(static_cast<rtit_parser_state*>(this)) = marked_parser_state_;
            return input_->seek(marked_input_position_);
        }

    private:
        shared_ptr<rtit_seekable_parser_input> input_;
        rtit_parser_state                      marked_parser_state_;
        rtit_pos                               marked_input_position_;
    };

} // namespace sat

#endif
