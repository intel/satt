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
#include "sat-ipt-instruction.h"
#include "sat-log.h"
#include <cinttypes>

namespace sat {

rva instruction::mcount_address_     = 0;
rva instruction::cmpxchg_address_    = 0;
rva instruction::copy_user1_address_ = 0;
rva instruction::copy_user2_address_ = 0;
//rva instruction::memcpy_address_     = 0;
//rva instruction::memset_address_     = 0;


// non-transfer instruction

class non_transfer : public instruction {
private:
    inline bool execute(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else {
            c.pc_ = next_address(c);
            return false;
        }
    }
public:
    virtual bool tip(context& c) const
    {
        return execute(c);
    }
}; // non_transfer


// direct transfer instructions

class direct_transfer : public instruction {
public:
    explicit direct_transfer(rva target) : target_(target) {}

    inline rva target(const context& c) const
    {
        return /* c.offset_ + */ target_;
    }

    virtual bool execute(context& c) const = 0;
protected:
    const rva target_;
}; // direct_transfer

class direct_jump : public direct_transfer {
private:
    inline bool execute(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else {
            c.pc_ = target(c);
            return false;
        }
    }
public:
    explicit direct_jump(rva target) : direct_transfer(target) {}
    virtual bool tip(context& c) const
    {
        return execute(c);
    }
}; // direct_jump

class direct_conditional : public direct_transfer {
private:
    inline bool execute(context& c) const
    {
        SAT_LOG(0, "ERROR: WE ARE IN execute() OF A DIRECT CONDITIONAL\n");
        c.get_lost();
        return true;
    }
public:
    explicit direct_conditional(rva target) : direct_transfer(target) {}
    virtual bool tip(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else if (!c.tnts_.empty()) {
            if (c.tnts_.taken()) {
                SAT_LOG(1, "taken\n");
                c.pc_ = target(c);
            } else {
                SAT_LOG(1, "not taken\n");
                c.pc_ = next_address(c);
            }
            SAT_LOG(1, "%u TNTs left\n", c.tnts_.size());
            return false;
        } else {
            SAT_LOG(0, "ERROR: CONDITIONAL WITHOUT TNT\n");
            c.get_lost();
            return true;
        }
    }
}; // direct_conditional

class direct_call : public direct_transfer {
private:
    inline bool execute(context& c) const
    {
        rva t    = target(c);
        rva next = next_address(c);

        if (fup_check(c)) {
            return true;
        } else if (t > c.pc_ && t < next) {
            // it is a call instruction with an address pointing to itself;
            // must be a relocation that the loader has patched
            if (!c.resolve_relocation(t)) {

                fprintf(stderr, "ERROR: unknown relocation\n");
                SAT_LOG(0, "ERROR: unknown relocation; WE ARE LOST\n");
                c.get_lost();
                return true;
            }
        } else if (t == next) {
            // It is a call to the next instruction;
            // assume it is a trick to obtain PC,
            // and treat it as a regular instruction.
            // We can be certain that the NLIP created by the call
            // is useless, so discard the stack item altogether to
            // avoid a warning when we get the TIP.
            goto adjust_pc;
        } else if (c.ignone_stack_manipulation_in_this_function_) {
            goto adjust_pc;
        }

        c.output_instructions_before_call();
        c.pending_output_call_ = true;
        //c.ret_stack_.push_back(next);
        c.call_stack_.push(next);
        SAT_LOG(1, "saving return address %" PRIx64 "\n", next);
        //printf("ret stack depth %u\n", (unsigned)c.ret_stack_.size());
        SAT_LOG(1, "ret stack depth %d\n", c.call_stack_.depth());
adjust_pc:
        c.pc_ = t;
        return false;
    }
public:
    explicit direct_call(rva target) : direct_transfer(target) {}
    virtual bool tip(context& c) const
    {
        return execute(c);
    }
}; // direct_call


// indirect transfer instructions

class indirect_transfer : public instruction {
public:
}; // indirect_transfer

class indirect_jump : public indirect_transfer {
public:
    virtual bool tip(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else {
            SAT_LOG(1, "using tip %" PRIx64 "\n", c.tip_);
            c.pc_ = c.tip_;
        }
        return true;
    }
}; // indirect_jump

class indirect_call : public indirect_transfer {
public:
    virtual bool tip(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else {
            rva next = next_address(c);
            c.pc_ = c.tip_;
            c.output_instructions_before_call();
            c.pending_output_call_ = true;
            //c.ret_stack_.push_back(next);
            c.call_stack_.push(next);
            SAT_LOG(1, "saving return address %" PRIx64 "\n", next);
            //printf("ret stack depth %u\n", (unsigned)c.ret_stack_.size());
            SAT_LOG(1, "ret stack depth %d\n", c.call_stack_.depth());
            SAT_LOG(1, "using tip %" PRIx64 "\n", c.tip_);
        }
        return true;
    }
}; // indirect_call

class indirect_syscall : public indirect_transfer {
public:
    virtual bool tip(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else {
            c.syscall_ = true;
            rva next = next_address(c);
            c.pc_ = c.tip_;
            c.output_instructions_before_call();
            c.pending_output_call_ = true;
            c.call_stack_.push(next);
            SAT_LOG(1, "saving return address %" PRIx64 "\n", next);
            SAT_LOG(1, "ret stack depth %d\n", c.call_stack_.depth());
            SAT_LOG(1, "using tip %" PRIx64 "\n", c.tip_);
        }
        return true;
    }
}; // indirect_syscall

class indirect_return : public indirect_transfer {
public:
    virtual bool tip(context& c) const
    {
        if (fup_check(c)) {
            return true;
        } else if (!c.tnts_.empty()) {
            if (c.tnts_.taken()) {
                c.output_instructions();
                c.pc_ = c.call_stack_.pop();
                if (c.pc_) {
                // if (!c.ret_stack_.empty()) {
                //     c.pc_ = c.ret_stack_.back();
                //     c.ret_stack_.pop_back();
                    SAT_LOG(1, "compressed ret %" PRIx64 "\n", c.pc_);
                    SAT_LOG(1, "ret stack depth %d\n",
                            c.call_stack_.depth());
                           //(unsigned)c.ret_stack_.size());
                    SAT_LOG(1, "%u TNTs left\n", c.tnts_.size());
                    return false;
                } else {
                    SAT_LOG(0, "ERROR: COMPRESSED RET WITH EMPTY STACK\n");
                    c.get_lost();
                    return true;
                }
            } else {
                SAT_LOG(0, "ERROR: COMPRESSED RET WITH N\n");
                c.get_lost();
                return true;
            }
        } else {
            SAT_LOG(1, "using tip %" PRIx64 "\n", c.tip_);
            c.output_instructions();
            c.pc_ = c.call_stack_.pop();
            if (c.pc_ && (c.pc_ != c.tip_)) {
                SAT_LOG(0, "ERROR: TIP does not match with return address from call_stack (%" PRIx64 ")\n", c.pc_);
            }
            c.pc_ = c.tip_;
            return true;
        }
    }
}; // indirect_return

class indirect_ireturn : public indirect_transfer {
public:
    virtual bool tip(context& c) const
    {
#if 0
        string function;
        //get_location(c.tip_, function);
        unsigned u = 0;
        if (find(c.call_stack_->begin(),
                 c.call_stack_->end(),
                 c.tip_) != c.call_stack_->end())
        {
            rva dropped;
            do {
                dropped = c.call_stack_->back();
                c.call_stack_->pop_back();
                ++u;
            } while (dropped != c.tip_);
        }
        printf("DROPPED %u RETURNING FROM AN INTERRUPT TO %s\n", u, function.c_str());
#endif

        //print_location(c.tip_);
        SAT_LOG(1, "using tip %" PRIx64 "\n", c.tip_);
        c.output_iret(c.tip_);
        c.pc_ = c.tip_;

        return true;
    }
};


instruction* instruction::make(rva address, const disassembled_instruction& i)
{
    instruction* result;

    // if (address == memcpy_address_ || address == memset_address_) {
    //     result = new indirect_return;
    if (i.is_transfer()) {
        if (i.has_jump_target()) {
            if (i.is_call()) {
                if (i.jump_target() == mcount_address_ ||
                    i.jump_target() == cmpxchg_address_)
                {
                    result = new non_transfer;
                } else if (i.jump_target() == copy_user1_address_) {
                    result = new direct_call(copy_user2_address_);
                } else {
                    result = new direct_call(i.jump_target());
                }
            } else if (i.is_conditional()) {
                result = new direct_conditional(i.jump_target());
            } else {
                if (i.jump_target() == copy_user1_address_) {
                    result = new direct_jump(copy_user2_address_);
                } else {
                    result = new direct_jump(i.jump_target());
                }
            }
        } else {
            if (i.is_call()) {
                result = new indirect_call;
            } else if (i.is_jump()) {
                result = new indirect_jump;
            } else if (i.is_return()) {
                result = new indirect_return;
            } else if (i.is_syscall()) {
                result = new indirect_syscall;
            } else {
                result = new indirect_ireturn;
            }
                // printf("TODO: INDIRECT IRET\n"); exit(EXIT_FAILURE);
                // result = new error_instruction(address); // TODO
        }
    } else {
        result = new non_transfer;
    }

    result->next_address_ = i.next_address();
    result->text_         = i.text();

    return result;
}

instruction_iterator::instruction_iterator(instruction_cache&       cache,
                                           shared_ptr<disassembler> disassembler,
                                           unsigned                 offset,
                                           rva                      address,
                                           const string&            symbol) :
    first_call_(true),
    cache_(cache),
    disassembler_(disassembler),
    offset_(offset),
    entry_point_(address),
    symbol_(symbol)
{
    seek(address);
}

bool instruction_iterator::seek(rva address)
{
    bool found = false;
    ici_ = cache_.lower_bound(address);
    if (ici_ != cache_.end() && ici_->first == address) {
        found = true;
    } else {
        found = disassemble(address);
    }
    return found;
}

const instruction* instruction_iterator::next()
{
    const instruction* result = 0;
    if (first_call_) {
        //printf("in first call, ici_->first is %" PRIx64 "\n", ici_->first);
        first_call_ = false;
        result = ici_->second;
    } else {
        //printf("in subsequent call at (%" PRIx64 ", %s)\n", ici_->first, ici_->second->text().c_str());
        rva next_address = ici_->second->next_address_;
        //printf("advancing\n");
        ++ici_;
        //printf("seeing where we got\n");
        if (ici_ == cache_.end() || ici_->first != next_address) {
            //printf("disassembling\n");
            disassemble(next_address);
        }
        //printf("checking\n");
        result = ici_->second;
    }
    return result;
}

bool instruction_iterator::disassemble(rva address)
{
    //printf("disassemble(%" PRIx64 ")\n", address);
    bool done = false;
    disassembled_instruction instr;
    instruction* i;
    if (disassembler_->disassemble(address, instr)) {
        //printf("disassembled\n");
        i = instruction::make(address, instr);
        //printf("made\n");
    } else {
        SAT_LOG(0, "UNABLE TO DISASSEMBLE %" PRIx64 "\n", address);
        i = new error_instruction(address);
    }
    if (ici_ != cache_.begin()) {
        //printf("reversing\n");
        --ici_;
    }
    //printf("inserting %" PRIx64 "\n", address);
    ici_ = cache_.insert(ici_, {address, i});
    //printf("inserted (%" PRIx64 ", %s)\n", ici_->first, ici_->second->text().c_str());
    done = true;
    return done;
}

const string& instruction_iterator::symbol()
{
    if (symbol_ == "") {
        // we don't have a symbol yet, resolve it now
        unsigned dummy;
        if (!disassembler_->get_function(entry_point_, symbol_, dummy)) {
            symbol_ = "unknown";
        }
    }

    return symbol_;
}

} // sat
