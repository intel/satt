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
#include "sat-disassembler.h"
#include "sat-mmapped.h"
#include "sat-log.h"
#include <udis86.h>
#include <sstream>
#include <map>
#include <cinttypes>


namespace sat {

class disassembler::impl {
public:
    impl() {}

    shared_ptr<mmapped>  executable_;
    const unsigned char* host_load_address_;    // buffer
    unsigned             size_;                 // buffer size
    rva                  target_load_address_;  // load address
    ud_t                 u_;                    // udis86
    unsigned             bits_;                 // 32/64-bit disassembler
// TODO: put all private nonvirtual members here
};


bool get_jump_target(const ud_t* u, rva& target)
{
    bool got_it = false;

    if (ud_insn_opr(u, 1) == 0) {
        const struct ud_operand* o = ud_insn_opr(u, 0);
        if (o && o->type == UD_OP_JIMM) {
            const uint64_t mask = 0xffffffffffffffffull >> (64 - u->dis_mode);

            got_it = true;

            switch (o->size) {
                case 8:
                    target = (u->pc + o->lval.sbyte)  & mask;
                    break;
                case 16:
                    target = (u->pc + o->lval.sword)  & mask;
                    break;
                case 32:
                    target = (u->pc + o->lval.sdword) & mask;
                    break;
                default:
                    target = 0;
                    got_it = false;
            }
        }
    }

    return got_it;
}

bool disassembled_instruction::parse(const disassembler::impl* i)
{
    const ud_t* u = &i->u_;

    text_ = ud_insn_asm(u);

    ud_mnemonic_code m = ud_insn_mnemonic(u);

    switch (m) {
    case UD_Icall:
        type_            = CALL;
        has_jump_target_ = get_jump_target(u, jump_target_);
        break;

    case UD_Isyscall:
    case UD_Isysenter:
        type_            = SYSCALL;
        has_jump_target_ = false;
        break;

    case UD_Ijmp:
        type_            = JUMP;
        has_jump_target_ = get_jump_target(u, jump_target_);
        break;

    case UD_Iret:
    case UD_Iretf:
    case UD_Isysret:
    case UD_Isysexit:
        type_            = RETURN;
        has_jump_target_ = false;
        break;

    case UD_Iiretd:
    case UD_Iiretq:
    case UD_Iiretw:
        type_            = IRETURN;
        has_jump_target_ = false;
        break;

    case UD_Iinvalid:
        if (ud_insn_len(u) == 2 &&
            ud_insn_ptr(u)[0] == 0x0f && ud_insn_ptr(u)[1] == 0x35)
        {
            type_            = 0;
            has_jump_target_ = false;
            m                = UD_Isysexit;
            text_            = "sysexit";
        } else if (ud_insn_len(u) == 3 &&
                   ud_insn_ptr(u)[0] == 0x0f &&
                   ud_insn_ptr(u)[1] == 0x01 &&
                   ud_insn_ptr(u)[2] == 0xf9)
        {
            // TODO: why does udis86 not recognize rdtscp; is it a bug?
            type_            = 0;
            has_jump_target_ = false;
            m                = UD_Irdtscp;
            text_            = "rdtscp";
        }
        break;

    default:
        if (m >= UD_Ija && m <= UD_Ijz) {
            type_            = CONDITIONAL;
            has_jump_target_ = get_jump_target(u, jump_target_);
        } else {
            type_            = 0;
            has_jump_target_ = false;
        }
    }

    return m != UD_Iinvalid;
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
                result->pimpl_->executable_          = executable;
                result->pimpl_->host_load_address_   = host_load_address;
                result->pimpl_->size_                = size;
                result->pimpl_->target_load_address_ = target_load_address;
                result->pimpl_->bits_ = bits;
                ud_t* u = &result->pimpl_->u_;
                ud_init(u);
                ud_set_mode(u, bits);
                ud_set_vendor(u, UD_VENDOR_INTEL);
                ud_set_syntax(u, UD_SYN_ATT);
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

        rva offset = address - pimpl_->target_load_address_;
        ud_t*                u = &pimpl_->u_;
        const unsigned char* p = pimpl_->host_load_address_ + offset;
        unsigned             s = pimpl_->size_              - offset;
        if (is_x86_64_func_region(address))
        {
            ud_set_mode(u, 64);
        } else {
            ud_set_mode(u, pimpl_->bits_);
        }
        ud_set_input_buffer(u, p, s);
        ud_set_pc(u, address);
#if 0
        printf("%lx (%x bytes), address %lx\n", (uint64_t)p, s, address);
#endif
        unsigned size = ud_disassemble(u); // TODO: use ud_decode()

        if (size) {
            done = instr.parse(pimpl_.get());
            instr.next_address_ = address + size;
            if (!done ) {
                printf("   %" PRIx64 ": COULD NOT DISASSEMBLE %s\n",
                       address, ud_insn_hex(u));
            }
        } else {
            printf("   %" PRIx64 ": NO SIZE\n", address);
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
