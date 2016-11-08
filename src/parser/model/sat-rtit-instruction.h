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
// TODO: fup_lip_
// TODO: fup.far jumps
#ifndef SAT_RTIT_INSTRUCTION_H
#define SAT_RTIT_INSTRUCTION_H

#include "sat-disassembler.h"
#include "sat-rtit-parser.h"
#include "sat-call-stack.h"
#include "sat-tid.h"
#include "sat-rtit-model.h"
#include <map>
#include <cinttypes>

namespace sat {

    using namespace std;


    class context {
    public:
        context() :
            tid_(), cpu_(), lost_(true), pc_(), entry_id_(),
            tsc_(), previously_output_tsc_(),
            last_call_nlip_(), call_stack_(make_shared<call_stack>()),
            next_schedule_offset_(),
            taken_(), tip_(), fup_far_(),
            instruction_count_(), previously_output_instruction_count_(),
            pending_output_call_()
        {}

        bool resolve_relocation(rva& target)
        {
            bool resolved = false;

            if (resolve_relocation_callback) {
                resolved = resolve_relocation_callback(target);
            }

            return resolved;
        }

        void get_lost()
        {
            lost_           = true;
            last_call_nlip_ = 0;

            pid_t thread_id;
            tid_get_thread_id(tid_, thread_id);
            SAT_LOG(0,
                    "CONTEXT:" \
                    " CPU: %u THREAD: %u PC: %" PRIx64 \
                    " TSC [%" PRIx64 "..%" PRIx64 "]\n",
                    cpu_, thread_id, pc_,
                    tsc_.begin, tsc_.end);
        }

        void maybe_output_timestamp()
        {
            if (previously_output_tsc_ != tsc_.begin) {
                printf("@ t %" PRIx64 "\n", // t for timestamp
                       tsc_.begin - global_initial_tsc);
                SAT_LOG(1, "ts %" PRIx64 " <- %" PRIx64 "\n",
                        tsc_.begin - global_initial_tsc, tsc_.begin);
                previously_output_tsc_  = tsc_.begin;
            }
        }

        // TODO: this can be removed along with sat-rtit-model.cpp
        void output_thread(tid_t tid)
        {
            maybe_output_timestamp();
            printf("@ s %u %u\n", cpu_, tid); // s for schedule
        }

        void output_schedule_in()
        {
            maybe_output_timestamp();
            printf("@ > %u\n", cpu_); // i for schedule in
        }

        void output_schedule_out()
        {
            maybe_output_timestamp();
            printf("@ < %u\n", cpu_); // i for schedule in
        }

        void output_module(unsigned id)
        {
            maybe_output_timestamp();
            printf("@ x %u\n", id); // x for transfer
        }

        void output_instructions(unsigned id)
        {
            printf("@ e %d %u %" PRIu64 "\n", // e for execute
                   call_stack_->depth(),
                   id,
                   instruction_count_ -
                   previously_output_instruction_count_);
            previously_output_instruction_count_ = instruction_count_;
        }

        void output_instructions()
        {
            if (instruction_count_ > previously_output_instruction_count_) {
                output_instructions(entry_id_);
            }
            maybe_output_timestamp();
        }

        void output_previous_instructions()
        {
            --instruction_count_;
            output_instructions();
            ++instruction_count_;
        }

        void output_instructions_before_call()
        {
            output_previous_instructions();
            previously_output_instruction_count_ = instruction_count_;
        }

        void output_call(unsigned function_id)
        {
            printf("@ c %d %u\n", // c for call
                   call_stack_->depth() - 1, function_id);
        }

        void output_iret(rva address)
        {
            output_previous_instructions();
            printf("@ r %d %" PRIx64 " (iret)\n", // r for return
                   call_stack_->depth(), address);
            previously_output_instruction_count_ = instruction_count_;
        }

        tid_t                  tid_;
        unsigned               cpu_;
        bool                   lost_;
        bool                   have_return_address_;
        rva                    pc_;
        unsigned               entry_id_;
        struct {
            uint64_t begin; // lower bound for the tsc
            uint64_t end;   // upper bound for the tsc
        }                      tsc_; // current tsc range: [begin..end)
        uint64_t               previously_output_tsc_;
        rva                    last_call_nlip_;
        shared_ptr<call_stack> call_stack_;
        rtit_offset            next_schedule_offset_;

        bool                   taken_;
        rva                    tip_;
        rva                    fup_far_;

        uint64_t               instruction_count_;
        uint64_t               previously_output_instruction_count_;

        bool                   pending_output_call_;

        function<bool(rva& target)> resolve_relocation_callback;

        // TODO: can we remove offset_, because it seems to be always zero?
        rva         offset_; // load address of the executable
    }; // class context

    class instruction {
        friend class instruction_iterator;
    public:
        static instruction* make(rva                             address,
                                 const disassembled_instruction& i);

        const string& text()         const { return text_; }

        inline rva next_address(const context& c) const
        {
            return c.offset_ + next_address_;
        }

        virtual bool tnt    (context& c) const = 0;
        virtual bool tip    (context& c) const = 0;
        virtual bool fup_far(context& c) const = 0;

#ifndef NO_SILLY_HEURISTICS
        static rva mcount_address_;
        static rva cmpxchg_address_;
        static rva copy_user1_address_;
        static rva copy_user2_address_;
        //static rva memcpy_address_;
        //static rva memset_address_;
#endif

    protected:
        rva    next_address_;
        string text_;
    };

    class error_instruction : public instruction {
    public:
        error_instruction(rva address)
        {
            next_address_ = address; // TODO: is this the right thing to do?
            text_         = "COULD NOT DISASSEMBLE";
        }

        virtual bool tnt    (context& c) const { c.get_lost(); return true; }
        virtual bool tip    (context& c) const { c.get_lost(); return true; }
        virtual bool fup_far(context& c) const { c.get_lost(); return true; }
    };

    typedef map<rva, instruction*> instruction_cache;

    class instruction_iterator {
    public:
        instruction_iterator(instruction_cache&       cache,
                             shared_ptr<disassembler> disassembler,
                             unsigned                 offset,
                             rva                      entry_point,
                             const string&            symbol);

        bool seek(rva address);
        const instruction* next();

        const string& symbol();

    private:
        bool disassemble(rva address);

        bool                        first_call_; // TODO: remove?
        instruction_cache&          cache_;
        instruction_cache::iterator ici_;
        shared_ptr<disassembler>    disassembler_;
        unsigned                    offset_;
        rva                         entry_point_;
        string                      symbol_;
    };

}

#endif
