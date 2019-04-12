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
#ifndef SAT_IPT_INSTRUCTION_H
#define SAT_IPT_INSTRUCTION_H

#include "sat-ipt-tnt.h"
#include "sat-tid.h"
#include "sat-disassembler.h"
#include "sat-call-stack.h"
#include "sat-ipt-model.h"
#include "sat-log.h"
#include <map>

namespace sat {

using namespace std;

struct context
{

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
        lost_     = true;
        printf("get_lost\n");
        // Clean tnt buffer.
        tnts_.clean();
        syscall_ = false;
    }

    void maybe_output_timestamp()
    {
        if (previously_output_tsc_ != tsc_.begin) {
            if (!fast_forward_) {
                printf("@ t %" PRIx64 "\n", // t for timestamp
                       tsc_.begin - global_initial_tsc);
            }
            SAT_LOG(1, "ts %" PRIx64 " <- %" PRIx64 "\n",
                    tsc_.begin - global_initial_tsc, tsc_.begin);
            previously_output_tsc_  = tsc_.begin;
        }
    }

    void force_output_timestamp()
    {
        if (!fast_forward_) {
            printf("@ t %" PRIx64 "\n", // t for timestamp
                   tsc_.begin - global_initial_tsc + 1);
        }
        SAT_LOG(1, "ts %" PRIx64 " <- %" PRIx64 "\n",
                tsc_.begin - global_initial_tsc + 1, tsc_.begin);
        previously_output_tsc_  = tsc_.begin + 1;
    }

    void output_schedule_in()
    {
        maybe_output_timestamp();
        if (!fast_forward_) {
            printf("@ > %u\n", cpu_); // i for schedule in
        }
    }

    void output_schedule_out()
    {
        force_output_timestamp();
        //maybe_output_timestamp();
        if (!fast_forward_) {
            printf("@ < %u\n", cpu_); // i for schedule in
        }
    }

    void output_module(unsigned id)
    {
        maybe_output_timestamp();
        if (!fast_forward_) {
            printf("@ x %u\n", id); // x for transfer
        }
    }

    void output_instructions(unsigned id)
    {
        if (!fast_forward_) {
            printf("@ e %d %u %" PRIu64 "\n", // e for execute
                   call_stack_.depth(),
                   id,
                   instruction_count_ -
                   previously_output_instruction_count_);
        }
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
        if (!fast_forward_) {
            printf("@ c %d %u\n", // c for call
               call_stack_.depth() - 1, function_id);
        }
    }

    void output_iret(rva address)
    {
        output_previous_instructions();
        if (!fast_forward_) {
            printf("@ r %d %" PRIx64 " (iret)\n", // r for return
                   call_stack_.depth(), address);
        }
        previously_output_instruction_count_ = instruction_count_;
    }

    // void set_pc(rva pc)
    // {
    //     if (have_psb_) {
    //         pc_      = pc;
    //         have_pc_ = true;
    //         if (have_tsc_) {
    //             lost_ = false;
    //         }
    //     }
    // }
    //
    // void set_tsc(uint64_t begin, uint64_t end)
    // {
    //     if (have_psb_) {
    //         tsc_.begin = begin;
    //         tsc_.end = end;
    //         have_tsc_ = true;
    //         if (have_pc_) {
    //             lost_ = false;
    //         }
    //     }
    // }

    void dump()
    {
        printf("TSC: %lx, PC: %lx\n", tsc_.begin, pc_);
    }

    tid_t       tid_;
    unsigned    cpu_;
    bool        lost_;
//    bool        have_psb_;
//    bool        have_pc_;
//    bool        have_tsc_;
    rva         pc_;
    unsigned    entry_id_;
    struct {
        uint64_t begin; // lower bound for the tsc
        uint64_t end;   // upper bound for the tsc
    }           tsc_; // current tsc range: [begin..end)
    uint64_t    previously_output_tsc_;
    rva         last_call_nlip_;
    rva         tip_;
    rva         fup_;
    //rva         offset_; // TODO: REMOVE as not needed
    tnt_buffer  tnts_;
    //vector<rva> ret_stack_;
    call_stack  call_stack_;
    uint64_t    instruction_count_;
    uint64_t    previously_output_instruction_count_;
    bool        pending_output_call_;
    function<bool(rva& target)> resolve_relocation_callback;
    uint32_t    exec_loop_count_;
    uint32_t    exec_loop_tnts_;
    uint64_t    exec_loop_ipt_location_;
    // fast-forward mode is enabled when we start processing new block and we execute
    //  from previous "return compression reset" point (PSB, PGE or OVF) to the real
    //  block starting point (basically scheduler_tip packet). No intermediate info
    //  messages are sent out during this phase.
    bool        fast_forward_;
    bool        syscall_;
    // Handle RETPOLINE mitigation
    // Special direct call handling flag, used in some cases
    bool        ignone_stack_manipulation_in_this_function_;
}; // context


class instruction {
    friend class instruction_iterator;
public:
    static instruction* make(rva address, const disassembled_instruction& i);

    const string& text() const { return text_; }

    inline rva next_address(const context& c) const
    {
        return /*c.offset_ +*/ next_address_;
    }

    virtual bool tip(context& c) const = 0;

    inline bool fup_check(context& c) const
    {
        bool r = false;
        // FUP can't be deferred, so wait until tnt buffer is empty
        if (c.tnts_.empty() && c.pc_ == c.fup_) {
            printf("fup %lx found, take fup action %lx\n", c.fup_, c.tip_);
            c.pc_ = c.tip_;
            c.fup_ = 0;
            r = true;
        }
        return r;
    }

    static rva mcount_address_;
    static rva cmpxchg_address_;
    static rva copy_user1_address_;
    static rva copy_user2_address_;
    //static rva memcpy_address_;
    //static rva memset_address_;

protected:
    rva    next_address_;
    string text_;
}; // instruction

class error_instruction : public instruction {
public:
    error_instruction(rva address)
    {
#if 0
        next_address_ = address; // TODO: is this the right thing to do?
#endif
        text_         = "COULD NOT DISASSEMBLE";
    }

    virtual bool tip    (context& c) const { c.get_lost(); printf("tip virtual impl\n"); return true; }
};


using instruction_cache = map<rva, instruction*>;

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
}; // instruction_iterator

} // sat

#endif // SAT_IPT_INSTRUCTION_H
