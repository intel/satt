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
#include "sat-disassembler-capstone.h"
#include "sat-mmapped.h"
#include "sat-log.h"
#include <capstone/capstone.h>
#include <sstream>
#include <map>
#include <cinttypes>
#include <string.h>


namespace sat {

class disassembler::impl {
public:
    impl() {}

    shared_ptr<mmapped>  executable_;
    const unsigned char* host_load_address_;    // buffer
    unsigned             size_;                 // buffer size
    rva                  target_load_address_;  // load address
    csh                  cps_h_;                // capstone handle
    cs_insn     *insn_;                // capstone instruction storage
    cs_mode     mode_;                 // 16/32/64-bit disassembler, model id
    unsigned    bits_;                 // 16/32/64-bit disassembler, numeric value
// TODO: put all private nonvirtual members here
};


bool get_jump_target(const disassembler::impl* i, rva& target)
{
    bool got_it = false;
    int index = cs_op_index(i->cps_h_, i->insn_, CS_OP_IMM, 1);
    if (index >= 0) {
        // Immediate operand found
        const cs_x86_op* o = &i->insn_->detail->x86.operands[0];
        got_it = true;
        target = o->imm;
    }

    return got_it;
}

bool disassembled_instruction::parse_missing_instructions(string text, bool jmp_target, unsigned type)
{
    text_ = text;
    type_ = type;
    has_jump_target_ = jmp_target;
    return true;
}

bool disassembled_instruction::parse(const disassembler::impl* i, uint16_t* size)
{
    text_ = i->insn_->mnemonic;
    text_ += " ";
    text_ += i->insn_->op_str;

    *size = i->insn_->size;

    // Default values
    type_            = 0;
    has_jump_target_ = false;

    // Check which group instruction belongs to
    int grp_count = i->insn_->detail->groups_count;
    if (grp_count > 0) {
        for (int g = 0; g < grp_count; g++) {
            int grp = i->insn_->detail->groups[g];
            if (grp < 128) {
                switch (grp) {
                case CS_GRP_CALL:
                    type_            = CALL;
                    has_jump_target_ = get_jump_target(i, jump_target_);
                    break;
                case CS_GRP_JUMP:
                    if (i->insn_->id >= X86_INS_JAE &&
                        i->insn_->id <= X86_INS_JS  &&
                        i->insn_->id != X86_INS_JMP &&
                        i->insn_->id != X86_INS_LJMP)
                    {
                        type_        = CONDITIONAL;
                    } else {
                        type_        = JUMP;
                    }
                    has_jump_target_ = get_jump_target(i, jump_target_);
                    break;
                case CS_GRP_RET:
                    type_            = RETURN;
                    has_jump_target_ = false;
                    break;
                case CS_GRP_IRET:
                    if (i->insn_->id == X86_INS_SYSEXIT ||
                        i->insn_->id == X86_INS_SYSRET)
                    {
                        type_            = RETURN;
                    } else {
                        type_            = IRETURN;
                    }
                    has_jump_target_ = false;
                    break;
                case CS_GRP_INT:
                    type_            = SYSCALL;
                    has_jump_target_ = false;
                    break;
                case CS_GRP_INVALID:
                default:
                    break;
                }
            }
        }
    }

    return (i->insn_->id != X86_INS_INVALID);
}


namespace {


// two-level cache for holding an arbitrary number of disassemblers
class disassembler_cache
{
public:
    static bool get_cached(const string&             host_path,
                           rva                       target_load_address,
                           shared_ptr<disassembler>& result)
    {
        bool got_it = false;

        if (host_path           == cached_host_path_ &&
            target_load_address == cached_target_load_address_)
        {
            result = cached_disassembler_;
            got_it = true;
        } else {
            auto i(second_level_cache_.find({host_path, target_load_address}));
            if (i != second_level_cache_.end()) {
                result = i->second;
                got_it = true;

                cached_host_path_           = host_path;
                cached_target_load_address_ = target_load_address;
                cached_disassembler_        = result;
            }
        }

        return got_it;
    }

    static void add(const string&             host_path,
                    rva                       target_load_address,
                    shared_ptr<disassembler>& d)
    {
        cached_host_path_           = host_path;
        cached_target_load_address_ = target_load_address;
        cached_disassembler_        = d;

        second_level_cache_.insert({{host_path, target_load_address}, d});
    }

private:
    typedef map<pair<string, rva>, shared_ptr<disassembler>> disassembler_map;

    static shared_ptr<disassembler> cached_disassembler_;
    static string                   cached_host_path_;
    static rva                      cached_target_load_address_;

    static disassembler_map         second_level_cache_;
};

// static
shared_ptr<disassembler> disassembler_cache::cached_disassembler_{};
string                   disassembler_cache::cached_host_path_{};
rva                      disassembler_cache::cached_target_load_address_{};
disassembler_cache::disassembler_map disassembler_cache::second_level_cache_{};

} // anonymous namespace


// static
shared_ptr<disassembler> disassembler::obtain(const string& host_path,
                                              const string& symbols_path,
                                              rva&          target_load_address)
{
    //printf("trying to obtain a disassembler for '%s':%lx\n", host_path.c_str(), target_load_address);
    shared_ptr<disassembler> result{nullptr};

    // TODO: should we resolve target paths to host paths here or in the caller?

#ifdef DISASSEMBLER_CACHE_STATISTICS
    static unsigned cache_hits{};
    static unsigned cache_misses{};
#endif

    if (!target_load_address) {
        shared_ptr<mmapped> executable = mmapped::obtain(host_path, symbols_path);
        if (executable && executable->is_ok()) {
            target_load_address = executable->default_load_address();
        }
    }

    if (!disassembler_cache::get_cached(host_path, target_load_address, result))
    {
        shared_ptr<mmapped> executable = mmapped::obtain(host_path, symbols_path);
        if (executable && executable->is_ok()) {
            const unsigned char* host_load_address;
            unsigned             size;
            unsigned             bits;
            if (executable->get_host_mmap(host_load_address, size, bits)) {
                result.reset(new disassembler);
                cs_mode     mode;
                csh         handle;
                switch (bits) {
                    case 16:
                        mode = CS_MODE_16;
                        break;
                    case 32:
                        mode = CS_MODE_32;
                        break;
                    case 64:
                    default:
                        mode = CS_MODE_64;
                }
                if (cs_open(CS_ARCH_X86, mode, &handle) != CS_ERR_OK) {
                    SAT_LOG(0, "failed to initialize disassembler\n");
                } else {
                    cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
                    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
                    result->pimpl_->insn_                = cs_malloc(handle);
                    result->pimpl_->cps_h_               = handle;
                    result->pimpl_->executable_          = executable;
                    result->pimpl_->host_load_address_   = host_load_address;
                    result->pimpl_->size_                = size;
                    result->pimpl_->target_load_address_ = target_load_address;
                    result->pimpl_->bits_                = bits;
                    result->pimpl_->mode_                = mode;
                }
            } else {
                SAT_LOG(0, "could not get host mmap\n");
            }
        }

        disassembler_cache::add(host_path, target_load_address, result);

#ifdef DISASSEMBLER_CACHE_STATISTICS
        ++cache_misses;
    } else {
        ++cache_hits;
#endif
    }

#ifdef DISASSEMBLER_CACHE_STATISTICS
    if (!((cache_hits + cache_misses) % 100000)) {
        fprintf(stderr, "disassembler cache hits / misses : %u / %u\n",
                cache_hits, cache_misses);
    }
#endif

    return result;
}


rva disassembler::target_load_address()
{
    return pimpl_->target_load_address_;
}

void disassembler::add_x86_64_region(rva begin, rva end)
{
    pimpl_->executable_->add_x86_64_region(begin, end);
}

bool disassembler::is_x86_64_func_region(rva address)
{
    return pimpl_->executable_->is_x86_64_region(address);
}

bool disassembler::disassemble(rva address, disassembled_instruction& instr)
{
    bool done = false;

    if (pimpl_->host_load_address_) {
#if 0
        printf("disassembling at %lx in [%lx..%lx) -> ",
               address,
               pimpl_->target_load_address_,
               pimpl_->target_load_address_ + pimpl_->size_);
#endif
        rva original_addr = address;
        rva offset = address - pimpl_->target_load_address_;
        csh                  cps_h = pimpl_->cps_h_;
        const unsigned char* p = pimpl_->host_load_address_ + offset;   // pointer to the actual code
        const unsigned char* orig_p = p;
        size_t               s = pimpl_->size_              - offset;   // Size of the code

        bool success = cs_disasm_iter(cps_h, &p, &s, &address, pimpl_->insn_);
#if 0
        printf("%lx (%lx bytes), address %lx\n", (uint64_t)p, s, original_addr);
#endif
        if (success) {
            uint16_t size;
            done = instr.parse(pimpl_.get(), &size);
            instr.next_address_ = address;  // address is already updated by cs_disasm_iter()
                                            // to point to next instruction
            if (!done ) {
                uint8_t* bytes = pimpl_->insn_->bytes;
                printf("   %" PRIx64 ": COULD NOT DISASSEMBLE - code: %02x %02x %02x %02x\n",
                       original_addr, bytes[0], bytes[1], bytes[2], bytes[3]);
            }
        } else {
            /* [ Fix: Missing opcodes in capstone */
            //printf("   %" PRIx64 ": COULD NOT DISASSEMBLE - code: %02x %02x %02x %02x\n",
            //    original_addr, orig_p[0], orig_p[1], orig_p[2], orig_p[3]);
            // xsaves64:
            if ( (orig_p[0] == 0x48) &&
                 (orig_p[1] == 0x0f) &&
                 (orig_p[2] == 0xc7) &&
                 (orig_p[3] == 0x2f))
                 {
                     done = instr.parse_missing_instructions("xsaves64 (%rdi)", false, 0);
                     instr.next_address_ = original_addr + 4;
                 }
            else if ( (orig_p[0] == 0x48) &&
                      (orig_p[1] == 0x0f) &&
                      (orig_p[2] == 0xc7) &&
                      (orig_p[3] == 0x1f))
                      {
                          done = instr.parse_missing_instructions("xrstors64 (%rdi)", false, 0);
                          instr.next_address_ = original_addr + 4;
                      }
            /* Fix: Missing opcodes in capstone ] */
            else {
                printf("   %" PRIx64 ": COULD NOT DISASSEMBLE - code: %02x %02x %02x %02x\n",
                    original_addr, orig_p[0], orig_p[1], orig_p[2], orig_p[3]);
                cs_err code = cs_err(cps_h);
                printf("   %" PRIx64 ": COULD NOT DISASSEMBLE - ERROR: %s\n", original_addr, cs_strerror(code));
            }
        }
    }

    return done;
}

bool disassembler::get_function(rva address, string& name, unsigned& offset)
{
    return pimpl_->executable_->get_function(
                                    address - pimpl_->target_load_address_,
                                    name,
                                    offset);
}

bool disassembler::get_global_function(string name, rva& address)
{
    bool got_it = pimpl_->executable_->get_global_function(name, address);
    if (got_it) {
        address += pimpl_->target_load_address_;
    }
    return got_it;
}

bool disassembler::get_relocation(rva address, string& name)
{
    return pimpl_->executable_->get_relocation(
                                    address - pimpl_->target_load_address_,
                                    name);
}

disassembler::disassembler()
    : pimpl_{new impl{}}
{
}

disassembler::~disassembler()
{
}

}
