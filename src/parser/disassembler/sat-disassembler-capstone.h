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
#ifndef SAT_DISASSEMBLER_H
#define SAT_DISASSEMBLER_H

#include "sat-types.h"
#include <memory>
#include <string>

namespace sat {

    using namespace std;

    class disassembled_instruction;

    class disassembler {
    public:
        static shared_ptr<disassembler> obtain(const string& host_path,
                                               const string& symbols_path,
                                               rva&          target_load_address);
        ~disassembler();

        rva target_load_address();
        bool disassemble(rva address, disassembled_instruction& instr);
        bool get_function(rva address, string& name, unsigned& offset);
        bool get_global_function(string name, rva& address);
        bool get_relocation(rva address, string& name);
        void add_x86_64_region(rva begin, rva end);

        class impl;
    private:
        disassembler();
        bool is_x86_64_func_region(rva address);

        unique_ptr<impl> pimpl_;
    };


    class disassembled_instruction {
        friend class disassembler;

    public:
        const string& text()         const { return text_; }
        rva           next_address() const { return next_address_; }

        bool          is_call()        const { return type_ & CALL;        }
        bool          is_syscall()     const { return type_ & SYSCALL;     }
        bool          is_conditional() const { return type_ & CONDITIONAL; }
        bool          is_jump()        const { return type_ & JUMP;        }
        bool          is_return()      const { return type_ & RETURN;      }
        bool          is_ireturn()     const { return type_ & IRETURN;     }

        bool          is_transfer() const
        {
            return type_ &
                   (CALL | SYSCALL | CONDITIONAL | JUMP | RETURN | IRETURN);
        }

        // TODO: rename has_jump_target() to has_target()
        bool          has_jump_target() const { return has_jump_target_; }
        rva           jump_target()     const { return jump_target_;  }

    private:
        enum {
            CALL        = 0x01,
            SYSCALL     = 0x02,
            CONDITIONAL = 0x04,
            JUMP        = 0x08,
            RETURN      = 0x10,
            IRETURN     = 0x20,
        };

        bool parse(const disassembler::impl* i, uint16_t* size);
        bool parse_missing_instructions(string text, bool jmp_target, unsigned type);

        string      text_;
        rva         next_address_;
        unsigned    type_;
        bool        has_jump_target_;
        rva         jump_target_;
    };
}

#endif
