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
#include "sat-rtit-model.h"
#include "sat-log.h"
#include "sat-rtit-instruction.h"
#include <algorithm>

namespace sat {

#ifndef NO_SILLY_HEURISTICS
    rva instruction::mcount_address_     = 0;
    rva instruction::cmpxchg_address_    = 0;
    rva instruction::copy_user1_address_ = 0;
    rva instruction::copy_user2_address_ = 0;
    //rva instruction::memcpy_address_     = 0;
    //rva instruction::memset_address_     = 0;
#endif

    class non_transfer : public instruction {
    private:
        inline void execute(context& c) const
        {
            c.pc_ = next_address(c);
        }
    public:
        virtual bool tnt(context& c) const
        {
            execute(c);
            return false;
        }
        virtual bool tip(context& c) const
        {
            execute(c);
            return false;
        }
        virtual bool fup_far(context& c) const
        {
            rva next = next_address(c);
            if (next == c.fup_far_) {
                SAT_LOG(1, "NLIP FUP.FAR. WAIT FOR THE TIP\n");
                return true;
            } else {
                execute(c);
                return false;
            }
        }
    };

    class direct_transfer : public instruction {
    public:
        direct_transfer(rva target) : target_(target) {}

        inline rva target(const context& c) const {
            return c.offset_ + target_;
        }

        virtual bool execute(context& c) const = 0;

        virtual bool fup_far(context& c) const
        {
            if (next_address(c) == c.fup_far_) {
                // TODO: not sure if these are even possible
                SAT_LOG(1, "DIRECT NLIP FUP.FAR. WAIT FOR THE TIP\n");
                return true;
            } else if (target(c) == c.fup_far_) {
                SAT_LOG(1, "DIRECT TARGET FUP.FAR. WAIT FOR THE TIP\n");
                execute(c);
                return true;
            } else {
                execute(c);
                return false;
            }
        }

    protected:
        const rva target_;
    };

    class direct_call : public direct_transfer {
    private:
        inline bool execute(context& c) const
        {
            rva t    = target(c);
            rva next = next_address(c);
            if (t > c.pc_ && t < next) {
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
                // is useless, so discard the NLIP altogether to avoid
                // a warning when we get the TIP.
                c.last_call_nlip_ = 0;
                goto adjust_pc;
            }

            c.output_instructions_before_call();
            c.pending_output_call_ = true;
            c.call_stack_->push(next);
            c.last_call_nlip_ = next;
adjust_pc:
            c.pc_             = t;
            return false;
        }
    public:
        explicit direct_call(rva target) : direct_transfer(target) {}
        virtual bool tnt(context& c) const
        {
            return execute(c);
        }
        virtual bool tip(context& c) const
        {
            return execute(c);
            return false;
        }
    };

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
        virtual bool tnt(context& c) const
        {
            if (c.taken_) {
                c.pc_ = target(c);
                //printf("          TAKEN\n");
            } else {
                c.pc_ = next_address(c);
                //printf("          NOT TAKEN\n");
            }
            return true;
        }
        virtual bool tip(context& c) const
        {
            SAT_LOG(0, "UNEXPECTED CONDITIONAL BEFORE TIP. WE ARE LOST\n");
            c.get_lost();
            return true;
        }
        virtual bool fup_far(context& c) const
        {
            SAT_LOG(0, "UNEXPECTED CONDITIONAL BEFORE FUP.FAR. WE ARE LOST\n");
            c.get_lost();
            return true;
        }
    };

    class direct_jump : public direct_transfer {
    private:
        inline bool execute(context& c) const
        {
            c.pc_ = target(c);
            return false;
        }
    public:
        explicit direct_jump(rva target) : direct_transfer(target) {}
        virtual bool tnt(context& c) const
        {
            return execute(c);
        }
        virtual bool tip(context& c) const
        {
            return execute(c);
        }
    };

    class indirect_transfer : public instruction {
    public:
        virtual bool tnt(context& c) const
        {
            SAT_LOG(0, "TIPLESS INDIRECT TRANSFER. WE ARE LOST\n");
            c.get_lost();
            return true;
        }
        virtual bool fup_far(context& c) const
        {
            if (next_address(c) == c.fup_far_) {
                SAT_LOG(1, "INDIRECT NLIP FUP.FAR. WAIT FOR THE TIP\n");
                return true;
            } else {
                SAT_LOG(0, "HUH? INDIRECT TRANSFER WITH A MISMATCHING FUP.FAR."
                        " ARE WE LOST?\n");
                c.get_lost();
                return true;
            }
        }
    };

    class indirect_call : public indirect_transfer {
    public:
        virtual bool tip(context& c) const
        {
            rva next = next_address(c);
            c.pc_             = c.tip_;
            c.last_call_nlip_ = next;
            c.output_instructions_before_call();
            c.pending_output_call_ = true;
            c.call_stack_->push(next);

            return true;
        }
    };

    class indirect_jump : public indirect_transfer {
    public:
        virtual bool tip(context& c) const
        {
            c.pc_ = c.tip_;
            return true;
        }
    };

    class indirect_return : public indirect_transfer {
    public:
        virtual bool tnt(context& c) const
        {
            if (!global_return_compression) {
                return indirect_transfer::tnt(c);
            } else {
                if (c.taken_) {
                    if (c.last_call_nlip_ != 0) {
                        c.output_instructions();
                        c.call_stack_->pop(c.last_call_nlip_);
                        c.pc_ = c.last_call_nlip_;
                        c.last_call_nlip_ = 0;
                        SAT_LOG(1, "        TAKEN\n");
                        return true;
                    } else {
                        SAT_LOG(0, "RETURN COMPRESSION: T WITHOUT LCN\n");
                        c.get_lost();
                        return true;
                    }
                } else {
                    SAT_LOG(0, "RETURN COMPRESSION: N\n");
                    c.get_lost();
                    return true;
                }
            }
        }
        virtual bool tip(context& c) const
        {
            if (!global_return_compression) {
                c.output_instructions();
                c.call_stack_->pop(c.tip_);
                return true;
            } else {
                c.output_instructions();
                if (c.last_call_nlip_ == 0) {
                    c.call_stack_->pop(c.tip_);
                } else {
                    if (c.tip_ == c.last_call_nlip_) {
                        SAT_LOG(0, "RETURN COMPRESSION: TIP EQUALS LCN\n");
                    } else {
                        SAT_LOG(0, "RETURN COMPRESSION: TIP != LCN\n");
                    }
                    c.call_stack_->pop(c.tip_);
                    c.last_call_nlip_ = 0;
                }
                return true;
            }
        }
    };

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
            c.output_iret(c.tip_);

            return true;
        }
    };


    // static
    instruction* instruction::make(rva                             address,
                                   const disassembled_instruction& i)
    {
        instruction* result;

#ifndef NO_SILLY_HEURISTICS
/*        if (address == memcpy_address_ || address == memset_address_) {
            result = new indirect_return;
        } else*/
#endif

        if (i.is_transfer()) {
            if (i.has_jump_target()) {
                if (i.is_call()) {
#ifndef NO_SILLY_HEURISTICS
                    if (i.jump_target() == mcount_address_ ||
                        i.jump_target() == cmpxchg_address_)
                    {
                        result = new non_transfer;
                    } else if (i.jump_target() == copy_user1_address_) {
                        result = new direct_call(copy_user2_address_);
                    } else {
#endif
                        result = new direct_call(i.jump_target());
#ifndef NO_SILLY_HEURISTICS
                    }
#endif
                } else if (i.is_conditional()) {
                    result = new direct_conditional(i.jump_target());
                } else {
#ifndef NO_SILLY_HEURISTICS
                    if (i.jump_target() == copy_user1_address_) {
                        result = new direct_jump(copy_user2_address_);
                    } else {
#endif
                        result = new direct_jump(i.jump_target());
#ifndef NO_SILLY_HEURISTICS
                    }
#endif
                }
            } else {
                if (i.is_call()) {
                    result = new indirect_call;
                } else if (i.is_jump()) {
                    result = new indirect_jump;
                } else if (i.is_return()) {
                    result = new indirect_return;
                } else {
                    result = new indirect_ireturn;
                }
            }
        } else {
            result = new non_transfer;
        }

        result->next_address_ = i.next_address();
        result->text_         = i.text();

        return result;
    }


    instruction_iterator::instruction_iterator(
                              instruction_cache&       cache,
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
        //address -= offset_;
        //printf("seeking to %" PRIx64 " (%" PRIx64 ")\n", address+offset_, address);
        //printf("seeking to %" PRIx64 "\n", address);
        //fflush(stdout);
        bool found = false;
        ici_ = cache_.lower_bound(address);
        if (ici_ != cache_.end() && ici_->first == address) {
            //printf("found in cache\n");
            //fflush(stdout);
            found = true;
        } else {
            //printf("not in cache\n");
            //fflush(stdout);
            found = disassemble(address);
        }
        return found;
    }

    const instruction* instruction_iterator::next()
    {
        const instruction* result = 0;
        if (first_call_) {
            //printf("in first call, ici_->first is %" PRIx64 "\n", ici_->first);
            //fflush(stdout);
            first_call_ = false;
            result = ici_->second;
        } else {
            //printf("in subsequent call at (%" PRIx64 ", %s)\n", ici_->first, ici_->second->text().c_str());
            //fflush(stdout);
            rva next_address = ici_->second->next_address_;
            //printf("advancing\n");
            //fflush(stdout);
            ++ici_;
            //printf("seeing where we got\n");
            //fflush(stdout);
            if (ici_ == cache_.end() || ici_->first != next_address) {
                //printf("disassembling\n");
                //fflush(stdout);
                disassemble(next_address);
            }
            //printf("checking\n");
            //fflush(stdout);
            result = ici_->second;
        }
        return result;
    }

    bool instruction_iterator::disassemble(rva address)
    {
        //printf("disassemble(%" PRIx64 ")\n", address);
        //fflush(stdout);
        bool done = false;
        disassembled_instruction instr;
        instruction* i;
        if (disassembler_->disassemble(address, instr)) {
            //printf("disassembled\n");
            //fflush(stdout);
            i = instruction::make(address, instr);
            //printf("made\n");
            //fflush(stdout);
        } else {
            SAT_LOG(0, "UNABLE TO DISASSEMBLE %" PRIx64 "\n", address);
            i = new error_instruction(address);
        }
#if 1
        if (ici_ != cache_.begin()) {
            //printf("reversing\n");
            //fflush(stdout);
            --ici_;
        }
#endif
        //printf("inserting %" PRIx64 "\n", address);
        //fflush(stdout);
        ici_ = cache_.insert(ici_, {address, i});
        //printf("inserted (%" PRIx64 ", %s)\n", ici_->first, ici_->second->text().c_str());
        //fflush(stdout);
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

}
