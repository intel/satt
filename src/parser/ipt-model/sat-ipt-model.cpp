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
#include "sat-demangle.h"
#include "sat-symbol-table.h"
#include "sat-symbol-table-file.h"
#include "sat-sideband-model.h"
#include "sat-ipt-block.h"
#include "sat-ipt-parser.h"
#include "sat-input.h"
#include "sat-ipt-collection.h"
#include "sat-ipt-instruction.h"
#include "sat-ipt-tsc-heuristics.h"
#include "sat-helper-path-mapper.h"
#include "sat-disassembler.h"
#include "sat-system-map.h"
#include "sat-log.h"
#include <memory>
#include <vector>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

using namespace std;

namespace {
    unsigned         global_ipt_buffer_overflow_count    = 0;
    sat::ipt_offset  global_ipt_input_skipped_bytes      = 0;
} // anonymous namespace

namespace sat {
uint64_t         global_initial_tsc                  = 0;
const uint32_t   non_terminating_loop_threshold      = 500;

template <class INPUT>
class ipt_output :
    public ipt_parser_output_base<ipt_output<INPUT>>
{
public:
    using token = ipt_parser_token<ipt_output<INPUT>>;

    ipt_output(INPUT& input) :
        got_to_eof_(), input_(input)
    {
        context_.get_lost();

        context_.resolve_relocation_callback =
            [this](rva& target) -> bool
            {
                return resolve_relocation(target);
            };
    }

    // ---vvv--- parser token handlers ---vvv---

    void short_tnt(token& t)
    {
        SAT_LOG(1, "%08lx: short tnt\n", input_.beginning_of_packet());
        context_.tnts_.append(t.tnt.bits, t.tnt.mask);
        SAT_LOG(1, "%u TNTs left\n", context_.tnts_.size());
    }

    void long_tnt(token& t)
    {
        SAT_LOG(1, "%08lx long tnt\n", input_.beginning_of_packet());
        context_.tnts_.append(t.tnt.bits, t.tnt.mask);
        SAT_LOG(1, "%u TNTs left\n", context_.tnts_.size());
    }

    void tip(token& t)
    {
        SAT_LOG(1, "%08lx: tip %08lx\n", input_.beginning_of_packet(), t.tip);
        context_.tip_ = t.tip;
        if (in_ovf_) {
            context_.pc_ = t.tip;
            in_ovf_ = false;
        }
        if (!context_.lost_) {
            if (context_.fup_) {
                SAT_LOG(1, "execute until fup\n");
            } else {
                SAT_LOG(1, "execute until tip\n");
            }
            execute_until_ipt_packet(&instruction::tip);
        } else {
            // TODO
            SAT_LOG(1, "we are lost!!!\n");
        }
    }

    void cbr(token& t)
    {
        SAT_LOG(1, "%08lx: cbr %02x\n", input_.beginning_of_packet(), t.cbr);
    }

    void tip_pgd(token& t)
    {
        SAT_LOG(1, "%08lx: tip.pgd %08lx\n", input_.beginning_of_packet(), context_.fup_);
        if (!context_.lost_ && context_.fup_) {
            printf("execute until tip.pgd\n");
            execute_until_ipt_packet(&instruction::tip);
        } else {
            // TODO
            SAT_LOG(1, "we are lost!!!\n");
        }
    }

    void tip_pge(token& t)
    {
        SAT_LOG(1, "%08lx: tip.pge %08lx\n", input_.beginning_of_packet(), t.tip);
        get_tsc(input_.absolute_beginning_of_packet());
        context_.pc_ = t.tip;
        context_.fup_ = 0;
        context_.tnts_.clean();
        context_.lost_ = false;
        in_ovf_ = false;
    }

    void fup(token& t)
    {
        SAT_LOG(1, "%08lx: fup %08lx\n", input_.beginning_of_packet(), t.tip);
        if (in_ovf_) {
            SAT_LOG(1, "set starting point after overflow %08lx\n", t.tip);
            context_.pc_ = t.tip;
            in_ovf_ = false;
        } else if (in_psb_) {
            //context_.set_pc(t.tip);
            context_.pc_ = t.tip;
        } else if (!context_.lost_) {
            context_.fup_ = t.tip;
            SAT_LOG(1, "checking fup action\n");
        }
    }

    void tsc(token& t)
    {
        uint64_t begin = context_.tsc_.begin;

        get_tsc(input_.beginning_of_packet());
        if (begin > context_.tsc_.begin) {
            SAT_ERR("ERROR on CPU %u @ %08" PRIx64 ": TSC %" PRIu64 " steps back in time (%" PRIx64 " -> %" PRIx64 ")\n",
                   context_.cpu_, input_.beginning_of_packet(), context_.tsc_.begin - begin, context_.tsc_.begin, begin);
            exit(EXIT_FAILURE);
        }

        SAT_LOG(1, "%08" PRIx64 ": TSC %" PRIx64 " -> %" PRIx64 "..%" PRIx64 "\n",
                input_.beginning_of_packet(),
                t.tsc,
                context_.tsc_.begin,
                context_.tsc_.end);
    }

    void mtc(token& t)
    {
        get_tsc(input_.beginning_of_packet());

        SAT_LOG(1, "%08" PRIx64 ": MTC %u -> TSC %" PRIx64 "..%" PRIx64 "\n",
                input_.beginning_of_packet(), t.ctc,
                context_.tsc_.begin, context_.tsc_.end);
    }

    void psb(token& t)
    {
        SAT_LOG(1, "%08lx: psb\n", input_.beginning_of_packet());
        in_psb_            = true;
    }

    void psbend(token& t)
    {
        SAT_LOG(1, "%08lx: psbend\n", input_.beginning_of_packet());
        in_psb_ = false;
        if (!context_.lost_ && !context_.tnts_.empty()) {
            SAT_LOG(1, "WARNING: tnt buffer not empty on PSB\n");
        }
        context_.lost_ = false;
        context_.tnts_.clean();

    }

    void ovf(token& t)
    {
        SAT_LOG(1, "%08lx: OVERFLOW\n", input_.beginning_of_packet());
        in_ovf_ = true;
        //context_.get_lost();
        ++global_ipt_buffer_overflow_count;
        context_.tnts_.clean();
        context_.fup_ = 0;
        in_psb_ = false;
        context_.lost_ = false;
        output_lost("overflow", global_ipt_buffer_overflow_count);
        get_tsc(input_.beginning_of_packet());
    }

    void eof(token& t)
    {
        got_to_eof_ = true;
    }

    // ---^^^--- parser token handlers ---^^^---
    //

    void set_ff_state(bool state)
    {
        context_.fast_forward_ = state;
        if(state) {
            SAT_LOG(1,"Fast-Forward STARTS\n");
            context_.call_stack_.temp_stack_enable();
        } else {
            SAT_LOG(1,"Fast-Forward ENDS, output tsc\n");
            context_.call_stack_.temp_stack_disable();

            SAT_LOG(1, "Orig: tsc.begin:%lx, end:%lx\n", context_.tsc_.begin, context_.tsc_.end);
            tsc_heuristics_[context_.cpu_]->get_tsc(input_.absolute_beginning_of_packet(),
                                                    context_.tsc_.begin,
                                                    context_.tsc_.end);
            SAT_LOG(1, "New: tsc.begin:%lx, end:%lx\n", context_.tsc_.begin, context_.tsc_.end);
            context_.maybe_output_timestamp();
        }
    }

    bool set_symbol_paths(const string& symbols_path,
                          const string& executables_path,
                          const string& host_executables_path)
    {
        bool ok = true;

        ok = symbols_.set_path(symbols_path) &&
             executables_.set_path(executables_path);

        if (ok && host_executables_path != "") {
            host_executables_ =
                make_shared<symbol_table_file>();
            ok = host_executables_->set_path(host_executables_path);
        }

        return ok;
    }

    bool set_system_map_path(const string path)
    {
        bool ok = kernel_map_.read(path);

        if (ok) {
            kernel_map_.get_address("this_cpu_cmpxchg16b_emu",
                                    instruction::cmpxchg_address_);
            SAT_LOG(1, "will skip calls to " \
                    "this_cpu_cmpxchg16b_emu at 0x%" \
                    PRIx64 "\n",
                    instruction::cmpxchg_address_);

            kernel_map_.get_address("copy_user_generic_unrolled",
                                    instruction::copy_user1_address_);
            kernel_map_.get_address("copy_user_enhanced_fast_string",
                                    instruction::copy_user2_address_);
            SAT_LOG(1, "will replace calls and jumps to" \
                    " copy_user_generic_unrolled by calls" \
                    " to copy_user_enhanced_fast_string: 0x%" \
                    PRIx64 " -> 0x%" PRIx64 "\n",
                    instruction::copy_user1_address_,
                    instruction::copy_user2_address_);
#if 0
            kernel_map_.get_address("__memcpy", instruction::memcpy_address_);
            SAT_LOG(1, "will replace __memcpy (0x%" PRIx64 ") body with a ret\n",
                    instruction::memcpy_address_);
            kernel_map_.get_address("__memset", instruction::memset_address_);
            SAT_LOG(1, "will replace __memset (0x%" PRIx64 ") body with a ret\n",
                    instruction::memset_address_);
#endif
        }

        return ok;
    }

    void set_host_filesystem(shared_ptr<path_mapper> filesystem)
    {
        host_filesystem_ = filesystem;
        //sideband_.set_host_filesystem(filesystem);
        if (host_filesystem_) {
            host_filesystem_->find_file("vmlinux", kernel_image_path_, kernel_image_path_);
            SAT_LOG(1, "kernel image path: '%s'\n", kernel_image_path_.c_str());
        }
    }

    void set_disassembly_output(bool show)
    {
        show_disassembly_ = show;
    }

    int stack_low_water_mark()
    {
        return context_.call_stack_.low_water_mark();
    }

    void set_cpu(int cpu) { context_.cpu_ = cpu; /* printf("CPU = %d\n", cpu); */ }

    void set_tsc(uint64_t begin, uint64_t end) {
        context_.tsc_.begin = begin;
        context_.tsc_.end = end;
    }

    void output_schedule(bool in) {
        if (in) {
            context_.output_schedule_in();
        } else {
            context_.output_schedule_out();
        }

    }

    void start_new_block_execution() {
        context_.fast_forward_ = true;
        context_.call_stack_.temp_stack_enable();
    }

    bool                                   got_to_eof_;
    sideband_model                         sideband_;
    vector<shared_ptr<tsc_heuristics>>     tsc_heuristics_;

private:
    void get_tsc(const ipt_pos& pos)
    {

        //tsc_heuristics_[context_.cpu_]->dump_tscs();
        tsc_heuristics_[context_.cpu_]->get_tsc(pos,
                                                context_.tsc_.begin,
                                                context_.tsc_.end);
        SAT_LOG(1, "get_tsc, tsc.begin:%lx, end:%lx\n", context_.tsc_.begin, context_.tsc_.end);
        //context_.have_tsc_ = true;
        SAT_LOG(1, "TIMESLOT TSC: [%" PRIx64 "..%" PRIx64 "), " \
                                 "[%" PRIx64 "..%" PRIx64 ")\n",
                context_.tsc_.begin,
                context_.tsc_.end,
                context_.tsc_.begin - global_initial_tsc,
                context_.tsc_.end   - global_initial_tsc);
    }

    bool get_instruction_iterator(rva                    address,
                                  uint64_t               tsc,
                                  class instruction_iterator*& ii,
                                  string&                target_path,
                                  string&                host_path,
                                  rva&                   start)
    {
        SAT_LOG(1, "getting instruction iterator for tsc %" PRIx64
                   ", addr %" PRIx64 "\n",
               tsc, address);
        bool got_it = false;

        string name;

        unsigned dummy;
        string sym_path;

        if (kernel_map_.get_function(address, name, dummy)) {
            target_path = "/vmlinux";
            host_path   = kernel_image_path_;
            sym_path   = host_path;
            start       = 0; // let the disassembler resolve the start address
        } else if (sideband_.get_target_path(address, tsc, target_path, start)) {
            SAT_LOG(1, "got target path '%s'\n", target_path.c_str());
            sym_path = target_path;
            host_filesystem_->find_file(target_path, host_path, sym_path);
        } else {
            SAT_LOG(1, "target path not found\n");
        }

        if (host_path != "") {
            SAT_LOG(1, "got host path '%s'\n", host_path.c_str());
            shared_ptr<disassembler> d(disassembler::obtain(host_path, sym_path, start));

            if (d) {
                {
                    string name; unsigned offset;
                    if (d->get_function(address, name, offset)) {
                        SAT_LOG(1, "got disassembler for function '%s'\n",
                               name.c_str());
                    } else {
                        SAT_LOG(1, "got disassembler\n");
                    }
                }

                instruction_cache* ic;
                auto ici = caches_.find(d.get());
                if (ici == caches_.end()) {
                    ic = new instruction_cache;
                    caches_.insert({d.get(), ic});
                } else {
                    ic = ici->second;
                }

                ii = new instruction_iterator(*ic, d, start, address, name);
                got_it = true;
            }
        }

        return got_it;
    } // get_instruction_iterator

    unsigned symbol_id(const string& symbol)
    {
        unsigned id;

        symbols_.get_new_id(symbol, id);

        return id;
    }

    void output_lost(const char* synthetic_symbol, uint64_t count)
    {
        context_.output_instructions();

        unsigned synthetic_id = symbol_id(synthetic_symbol);
        if (show_disassembly_) {
            printf("@ d %u %d %s\n",
                   context_.cpu_,
                   context_.call_stack_.depth(),
                   synthetic_symbol);
        }
        // synthetic instruction count of one to get a unique timestamp
        context_.instruction_count_++;
        context_.output_instructions(synthetic_id);

        // output for the UI to display stats to the user
        printf("@ ! %c %" PRIu64 "\n", synthetic_symbol[0], count);
    }


    bool execute_until_ipt_packet(
         bool (instruction::* execute)(context& c) const)
     {
         bool done_with_packet = false;
         bool same_stack       = true;

         if (context_.call_stack_.depth() == 100) {
             static bool stack_dumped = false;
             if (!stack_dumped) {
                 SAT_LOG(0, "CALL STACK HAS GROWN SUSPICIOUSLY DEEP:\n");
                 context_.call_stack_.iterate([this](rva return_address) {
                     SAT_LOG(0, "%" PRIx64 " %s\n",
                             return_address,
                             get_location(return_address).c_str());
                 });
                 stack_dumped = true;
             }
         }

         // TODO: can we remove the check for !context_.lost here?
         while (!context_.lost_ && !done_with_packet) {
             // TODO: these statics are unsafe if we ever go parallel
             static string target_path{};
             static rva    load_address{};

             string old_path(target_path);
             rva    old_load_address(load_address);

             string host_path;

             sideband_.adjust_for_hooks(context_.pc_);

             class instruction_iterator* ii;
             if (!get_instruction_iterator(context_.pc_,
                                           context_.tsc_.begin,
                                           ii,
                                           target_path,
                                           host_path,
                                           load_address))
             {
                 SAT_LOG(1, "...could not get iterator\n");
                 context_.get_lost();
                 break;
             }

             if (context_.instruction_count_ ==
                   context_.previously_output_instruction_count_ ||
                 context_.pending_output_call_ ||
                 target_path != old_path ||
                 load_address != old_load_address)
             {
                 // save the point of entry to a stream of instructions
                 unsigned entry_id = symbol_id(ii->symbol());

                 if (context_.pending_output_call_) {
                     context_.output_call(entry_id);
                     context_.pending_output_call_ = false;
                 }

                 if (target_path  != old_path ||
                     load_address != old_load_address)
                 {
                     context_.output_instructions();
                     unsigned id;
                     if (executables_.get_new_id(target_path, id) &&
                         host_executables_)
                     {
                         // we entered a file we have not been in before;
                         // store the corresponding host path with id as well
                         host_executables_->insert(host_path, id);
                     }
                     context_.output_module(id);
                     if (show_disassembly_ && !context_.fast_forward_) {
                         printf("@ d %u %d %u@%" PRIx64 " (%s)\n",
                                context_.cpu_,
                                context_.call_stack_.depth(),
                                id,
                                load_address,
                                target_path.c_str());
                     }
                 }

                 context_.entry_id_ = entry_id;
             }

             if (show_disassembly_ && !done_with_packet && !context_.fast_forward_) {
                 printf("@ d %u %d -> %s\n",
                        context_.cpu_,
                        context_.call_stack_.depth(),
                        get_location(context_.pc_).c_str());
             }
             rva next_address;
             rva entry_pc;
             rva previous_pc;

             entry_pc = context_.pc_;
             do {
                 const instruction* i = ii->next(); // next() must be lazy!
                 ++context_.instruction_count_;

                 if (show_disassembly_ && !context_.fast_forward_) {
                     printf("@ d %u %d %" PRIx64 ": %s\n",
                            context_.cpu_,
                            context_.call_stack_.depth(),
                            context_.pc_,
                            i->text().c_str());
                 }
                 //printf("%" PRIx64 ": [%02" PRIu64 "] %s\n", context_.pc_, context_.call_stack_.size(), i->text().c_str());
                 next_address = i->next_address(context_);
                 previous_pc  = context_.pc_;
                 done_with_packet = ((*i).*execute)(context_);


                 // NOTE: We make a big assumption here that the next
                 //       address is found in the current executable.
                 //       In practice the assumption holds, but it would
                 //       be possible to construct a pathological scenario
                 //       where we move from one mmap()ped executable
                 //       to another without jumping further.
             } while (!done_with_packet && context_.pc_ == next_address);
#if 0
             if (show_disassembly_ && !done_with_packet) {
                 printf("@ d %u %d -> %s\n",
                        context_.cpu_,
                        context_.call_stack_.depth(),
                        get_location(context_.pc_).c_str());
             }
#endif
             if (!done_with_packet && !context_.lost_ &&
                 entry_pc <= context_.pc_ && context_.pc_ <= previous_pc)
             {
                 ++context_.exec_loop_count_;
                 if (non_terminating_loop_threshold < context_.exec_loop_count_) {
                     // Printing non-terminating loop warning only as debug print
                     SAT_LOG(1, "@ ! iPOSSIBLE NON-TERMINATING LOOP DETECTED\n");
                     context_.exec_loop_count_ = 0;
                 }
                 // TODO How to detect not-terminating loop preoperly???
                 #if 0
                 sleep(1);
                 context_.get_lost();
                 #endif
             } else {
                 context_.exec_loop_count_ = 0;
             }
             delete ii; // TODO: consider caching

             if (context_.lost_) {
                 output_lost("lost", 1);
             }
         }

         if (context_.pending_output_call_) {
             context_.output_call(symbol_id(symbol(context_.pc_)));
             context_.pending_output_call_ = false;
         }

         return same_stack;
     }

     string symbol(rva address)
     {
         string   result;
         unsigned offset; // TODO: use it or remove it
         string   path;
         string   sym_path;
         rva      start;

         if (kernel_map_.get_function(address, result, offset))
         {
             // it is a kernel address
         } else if (sideband_.get_target_path(address,
                                               context_.tsc_.begin,
                                               path,
                                               start))
         {
             // it is a userspace address
             host_filesystem_->find_file(path, path, sym_path);
             shared_ptr<disassembler> d(disassembler::obtain(path, sym_path, start));
             SAT_LOG(1, "symbol(%" PRIx64 "/%" PRIx64 ")\n", address, address - start);
             if (!d || !d->get_function(address, result, offset)) {
                 result = "unknown";
             }
         } else {
             result = "unknown";
         }

         return result;
     }


    string get_location(rva address)
    {
        string   result;
        unsigned offset; // TODO: use it or remove it
        string   path;
        string   sym_path;
        rva      start;

        if (kernel_map_.get_function(address, result, offset))
        {
            // it is a kernel address
            result = string("KERNEL:") + result;
        } else if (sideband_.get_target_path(address,
                                              context_.tsc_.begin,
                                              path,
                                              start))
        {
            // it is a userspace address
            host_filesystem_->find_file(path, path, sym_path);
            string f;
            shared_ptr<disassembler> d(disassembler::obtain(path, sym_path, start));
            SAT_LOG(1, "get_function(%" PRIx64 "/%" PRIx64 ")\n", address, address - start);
            if (!d || !d->get_function(address, f, offset)) {
                ostringstream a;
                a << hex << address;
                f = a.str();
            }
            result = path + ": " + f;
        }

        return result;
    }


    bool resolve_relocation(rva& target)
    {
        bool resolved_it = false;
        SAT_LOG(0, "RESOLVING %" PRIx64 " :|\n", target);
        string target_path;
        rva    start;
        if (!sideband_.get_target_path(target,
                                        context_.tsc_.begin,
                                        target_path,
                                        start))
        {
            return false;
        }
        string host_path;
        string sym_path;
        host_filesystem_->find_file(target_path, host_path, sym_path);
        shared_ptr<disassembler>
            d(disassembler::obtain(host_path, sym_path, start));

        string name;
        if (!d || !d->get_relocation(target, name)) {
            return false;
        }

        SAT_LOG(0, "RESOLVING %s :)\n", name.c_str());

        sideband_.iterate_target_paths(
                       context_.tid_,
                       context_.tsc_.begin,
                       [&](const string& target_path, rva start) -> bool
                       {
                           SAT_LOG(3, "CONSIDERING %s @ %" PRIx64 "\n",
                                   target_path.c_str(),
                                   start);

                           string host_path;
                           string sym_path;
                           host_filesystem_-> find_file(target_path,
                                                        host_path, sym_path);
                           shared_ptr<disassembler>
                               d(disassembler::obtain(host_path, sym_path, start));
                           resolved_it =
                               d && d->get_global_function(name, target);
                           if (resolved_it) {
                               SAT_LOG(0, "FOUND %s in %s @ %" PRIx64 " :D\n",
                                       name.c_str(),
                                       target_path.c_str(),
                                       target);
                           }
                           return resolved_it;
                       });
        return resolved_it;
    }

    INPUT&                                 input_;
    bool                                   in_psb_;
    bool                                   in_ovf_;
    context                                context_;
    system_map                             kernel_map_;
    string                                 kernel_image_path_;
    shared_ptr<path_mapper>                host_filesystem_;

    map<disassembler*, instruction_cache*> caches_;

    bool                                   show_disassembly_;

    file_backed_symbol_table               symbols_;
    file_backed_symbol_table               executables_;
    shared_ptr<symbol_table_file>          host_executables_;
}; // ipt_output

class ipt_model : public ipt_parser<input_from_file_block, ipt_output>
{
public:
    ipt_model(const ipt_collection& collection,
            const string&          symbols_path,
            const string&          executables_path,
            const string&          host_executables_path,
            const string&          system_map_path,
            bool                   show_disassembly,
            shared_ptr<path_mapper> host_filesystem) :
        collection_(collection) //, host_filesystem_(host_filesystem)
    {
        output().set_disassembly_output(show_disassembly);
        // if (!output().set_symbol_paths(symbols_path,
        //                                executables_path,
        //                                host_executables_path))
        // {
        //     printf("Setting symbol paths failed!!\n");
        //     exit(EXIT_FAILURE);
        // }

        output().set_host_filesystem(host_filesystem);
        if (system_map_path != "") {
            if (!output().set_system_map_path(system_map_path)) {
                SAT_LOG(0, "could not read System.map '%s'\n",
                    system_map_path.c_str());
                exit(EXIT_FAILURE);
            } else {
                SAT_LOG(0, "System.map: '%s'\n", system_map_path.c_str());
            }
        } else {
            SAT_LOG(0, "not using System.map\n");
        }
        output().sideband_.set_host_filesystem(host_filesystem);
        if (!output().sideband_.build(collection.sideband_path())) {
            printf("sideband building failed\n");
            exit(EXIT_FAILURE);
        } else {
            SAT_LOG(1, "built sideband\n");
        }

        for (auto& path : collection.ipt_paths()) {
            auto heuristics = make_shared<tsc_heuristics>(collection.sideband_path());
            heuristics->parse_ipt(path);
            heuristics->apply();
            output().tsc_heuristics_.push_back(heuristics);
        }

#if 1
        tid_t tid;
        uint32_t pkt_mask;
        output().sideband_.get_initial(0, tid, pkt_mask);
        SAT_LOG(1, "initial tid: %d\n", tid);
#endif
    }

    void set_host_filesystem(shared_ptr<path_mapper> host_filesystem)
    {
        output().set_host_filesystem(host_filesystem);
    }

    bool run(tid_t tid)
    {
        bool ok   = true;
        auto task = collection_.task(tid);

        if(!output().sideband_.set_tid_for_target_path_resolution(tid))
        {
            SAT_LOG(0, "ERROR: Cannot set tid for sideband!!\n");
            fprintf(stderr, "ERROR: Cannot set tid for sideband!!\n");
            exit(EXIT_FAILURE);
        }
        task->iterate_blocks([&](shared_ptr<ipt_block> block) {
            output().set_cpu(block->cpu_);
            switch (block->type_) {
            case ipt_block::TRACE:
                ok = run(collection_.ipt_paths()[block->cpu_], block);
                break;
            case ipt_block::SCHEDULE_IN:
                output().set_tsc(block->tsc_.first, block->tsc_.second);
                output().output_schedule(true);
                break;
            case ipt_block::SCHEDULE_OUT:
                output().set_tsc(block->tsc_.first, block->tsc_.second);
                output().output_schedule(false);
                break;
            default:
            break;
            } // switch
            return ok;
        });

        return ok;
    } // run()

    int stack_low_water_mark()
    {
        return output().stack_low_water_mark();
    }

    bool set_symbol_paths(const string& symbols_path,
                          const string& executables_path,
                          const string& host_executables_path)
    {
        return output().set_symbol_paths(symbols_path,
                                       executables_path,
                                       host_executables_path);
    }

private:
    bool run(const string& ipt_path, shared_ptr<ipt_block> block)
    {
        bool ok = true;

        ok = input().open(ipt_path, block->pos_.first, block->pos_.second, block->psb_);

        if (ok) {
            output().start_new_block_execution();
            while (parse()) {}
            ok = output().got_to_eof_;
        }

        return ok;
    } // run()

    const ipt_collection&     collection_;
    shared_ptr<path_mapper>   host_filesystem_;
    vector<string>            ipt_paths_;
}; // ipt_model

void report_warnings()
{
    if (global_ipt_buffer_overflow_count) {
        printf("@ ! iWARNING: there were %u IPT buffer overflows\n",
                global_ipt_buffer_overflow_count);
    }

    if (global_ipt_input_skipped_bytes) {
        printf("@ ! iWARNING: total of %" PRIu64 " bytes of IPT input were not parsable\n",
                global_ipt_input_skipped_bytes);
    }
}

string make_path(const string& format, tid_t tid)
{
    string path;

    if (format.find("%u") != string::npos) {
        char* p = 0;
        if (asprintf(&p, format.c_str(), tid) == -1) {
            fprintf(stderr, "out of memory\n");
            exit(EXIT_FAILURE);
        }
        path = p;
        free(p);
    } else {
        path = format;
    }

    return path;
}

void run(tid_t                      tid,
         shared_ptr<ipt_model>      model,
         shared_ptr<helper_path_mapper> host_filesystem,
         const string&              symbols_path,
         const string&              executables_path,
         const string&              host_executables_path,
         const string&              output_path_format,
         const string&              stack_low_water_marks_path_format)
{
    // replace stdout with a file
    int    output_file = -1;
    string output_path = make_path(output_path_format, tid);

    close(STDOUT_FILENO);
    output_file = creat(output_path.c_str(), S_IRUSR|S_IWUSR);
    if (output_file == -1) {
        fprintf(stderr, "cannot open file '%s' for writing model\n",
                output_path.c_str());
        exit(EXIT_FAILURE);
    }

    if (!model->set_symbol_paths(symbols_path,
                                   executables_path,
                                   host_executables_path))
    {
        SAT_LOG(0, "Setting symbol paths failed!!\n");
        exit(EXIT_FAILURE);
    }

    model->set_host_filesystem(host_filesystem);
    // obtain the task
    // SAT_LOG(0, "picking task ID '%u'\n", tid);
    // auto task = collection.task(tid);
    // if (!task) {
    //     SAT_ERR("no such task ID: %u\n", tid);
    //     exit(EXIT_FAILURE);
    // }

    // run the model
    SAT_LOG(0, "running IPT model with task ID '%u'\n", tid);
    SAT_LOG(0, "earliest tsc: %lx\n", global_initial_tsc);
    if (!model->run(tid)) {
        printf("@ ! iWARNING: IPT input for task %u ended abruptly\n", tid);
    }

    report_warnings();

    if (stack_low_water_marks_path_format != "") {
        string slwm_path = make_path(stack_low_water_marks_path_format, tid);
        FILE* slwm_file = fopen(slwm_path.c_str(), "wb");
        if (slwm_file) {
            fprintf(slwm_file, "%u|%d\n",tid, model->stack_low_water_mark());
            fclose(slwm_file); // TODO: check for errors
        }
    }

    fflush(stdout);
    if (output_file != -1) {
        close(output_file);
    }


}

} // namespace sat

#if 0
void print_progress_header()
{
  printf("| started |  ready  |\n");
  printf("=====================\n");
}

void print_progress(unsigned started, unsigned completed)
{
  printf("\r|   %3u%%  |   %3u%%  |     ",
      started,
      completed);
  fflush(stdout);
}
#else
void print_progress_header()
{
  printf("o: Started processing tasks\n");
  printf("#: Completed processing tasks\n");
  printf("=============================\n");
}

#define PROGRESS_BUFFER_SIZE 50
void print_progress(unsigned started, unsigned completed, unsigned max)
{
    char output[PROGRESS_BUFFER_SIZE + 1];
    unsigned i;
    unsigned s = (unsigned)( (started*PROGRESS_BUFFER_SIZE) / max);
    unsigned c = (unsigned)( (completed*PROGRESS_BUFFER_SIZE) / max);

    for (i = 0; i < c; i++) {
        output[i] = '#';
    }
    for (;i < s; i++) {
        output[i] = 'o';
    }
    for (;i<PROGRESS_BUFFER_SIZE;i++) {
        output[i] = '-';
    }
    output[i] = 0;
    printf("\r|%s|", output);
    fflush(stdout);
}
#endif


void usage(const char* name)
{
    printf("Usage: %s -C <collection-file>" \
           "\n",
           name);
}

int main(int argc, char* argv[])
{
    using namespace sat;

    bool            show_disassembly = false;
    string          collection_path;
    string          kernel_image_path;
    string          path_mapper_helper_command_format;
    string          path_mapper_cache_dir_path;
    string          system_map_path;
    string          executables_path;
    string          host_executables_path;
    string          symbols_path;
    unsigned        max_processes = 3; // default parallel processes
    // default path formats
    string          output_path_format = "task%u.model";
    string          stack_low_water_marks_path_format; // no output by default

    //global_use_stderr = false;
    // process command line switches
    int c;
    while ((c = getopt(argc, argv, ":C:dDe:f:F:h:lm:n:o:P:w:")) != EOF) {
        switch (c) {
        case 'C':
            collection_path = optarg;
            break;
        case 'd':
            show_disassembly = true;
            break;
        case 'D':
            ++global_debug_level;
            break;
        case 'e':
            executables_path = optarg;
            break;
        case 'f':
            path_mapper_helper_command_format = optarg;
            break;
        case 'F':
            path_mapper_cache_dir_path = optarg;
            break;
        case 'h':
            host_executables_path = optarg;
            break;
        case 'l': // log messages also to stderr
            global_use_stderr = true;
            break;
        case 'm':
            system_map_path = optarg;
            break;
        case 'n':
            symbols_path = optarg;
            break;
        case 'o':
            output_path_format = optarg;
            break;
        case 'P':
            if (sscanf(optarg, "%u", &max_processes) != 1) {
                fprintf(stderr,
                        "must specify max # of parallel processes with -P\n");
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;
        case 'w':
            stack_low_water_marks_path_format = optarg;
            break;
        case '?':
            fprintf(stderr, "unknown option '%c'\n", optopt);
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        case ':':
            fprintf(stderr, "missing filename to '%c'\n", optopt);
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    SAT_LOG(1, "show_disassembly; %d\n", show_disassembly);
    // sanity check command line switches
    if (symbols_path == "") {
        fprintf(stderr, "must specify symbols file for output with -n\n");
        exit(EXIT_FAILURE);
    } else {
        // truncate file
        FILE* symbols_file = fopen(symbols_path.c_str(), "wb");
        if (!symbols_file) {
            SAT_ERR("cannot open symbols file for output: '%s'\n",
                    symbols_path.c_str());
            exit(EXIT_FAILURE);
        }
        fclose(symbols_file);
    }

    if (executables_path == "") {
        fprintf(stderr, "must specify executables file for output with -e\n");
        exit(EXIT_FAILURE);
    } else {
        // truncate file
        FILE* executables_file = fopen(executables_path.c_str(), "wb");
        if (!executables_file) {
            SAT_ERR("cannot open executables file for output: '%s'\n",
                    executables_path.c_str());
            exit(EXIT_FAILURE);
        }
        fclose(executables_file);
    }

    if (path_mapper_helper_command_format == "") {
        fprintf(stderr, "must specify path mapper command with -f\n");
        exit(EXIT_FAILURE);
    }
    if (path_mapper_cache_dir_path == "") {
        fprintf(stderr, "must specify path mapper cache dir with -F\n");
        exit(EXIT_FAILURE);
    }

    if (collection_path == "") {
        fprintf(stderr, "must specify collection path with -C\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // set up mapper for finding target files from host filesystem
    shared_ptr<helper_path_mapper> host_filesystem =
        make_shared<helper_path_mapper>(path_mapper_cache_dir_path,
                                        path_mapper_helper_command_format);
    //helper_path_mapper host_filesystem(path_mapper_cache_dir_path,
    //                                   path_mapper_helper_command_format);

    host_filesystem->find_file("vmlinux", kernel_image_path, kernel_image_path);

    shared_ptr<system_map> kernel_map;
    if (system_map_path != "") {
        kernel_map = make_shared<system_map>();
        kernel_map->read(system_map_path);
    }

    // deserialize the trace collection
    SAT_LOG(0, "deserializing tasks from '%s'\n", collection_path.c_str());
    ifstream collection_stream(collection_path);
    if (!collection_stream) {
        fprintf(stderr, "cannot open collection '%s' for reading\n",
                collection_path.c_str());
        exit(EXIT_FAILURE);
    }
    ipt_collection collection(collection_stream);
    if (!collection) {
        fprintf(stderr, "cannot deserialize collection '%s'\n",
                collection_path.c_str());
        exit(EXIT_FAILURE);
    }

    global_initial_tsc = collection.earliest_tsc();
    SAT_LOG(0, "earliest tsc: %lx\n", global_initial_tsc);

    SAT_LOG(0, "creating IPT model\n");

    auto model = make_shared<ipt_model>(collection,
                                        symbols_path,
                                        executables_path,
                                        host_executables_path,
                                        system_map_path,
                                        show_disassembly,
                                        host_filesystem);

    // prime the model memory maps
    SAT_LOG(0, "priming memory maps\n");
    {
        rva dummy;
        shared_ptr<disassembler> dummy2(disassembler::obtain(kernel_image_path,
                                                             kernel_image_path,
                                                             dummy));
    }

    // loop through tasks forking processes
    host_filesystem->close(); // close before fork() to avoid sharing cache fd
    SAT_LOG(0, "running IPT model of %u tasks with %u parallel processes\n\n",
            collection.tasks(),
            max_processes);
    //SAT_LOG(0, "| started |  ready | total |\n");
    //SAT_LOG(0, "============================\n");
    print_progress_header();
    unsigned forked  = 0;
    unsigned running = 0;
    auto tids = collection.tids_in_decreasing_order_of_trace_size();
    for (auto tid : tids) {
        // fork a process for each task
        pid_t pid;
        switch (pid = fork()) {
        case -1:
            // TODO
            break;
        case 0:
            // child
            {
                // re-open path mapper cache files after fork()
                host_filesystem->open();
                run(tid,
                    model,
                    host_filesystem,
                    symbols_path,
                    executables_path,
                    host_executables_path,
                    output_path_format,
                    stack_low_water_marks_path_format);
                goto child_exit;
            }
            break;
        default:
            // parent
            ++forked;
            ++running;
            print_progress(forked, (forked-running), tids.size());
            SAT_LOG(1, "forked pid %u: %u/%" PRIu64
                       ", tid: %u, size: %lu, running: %u\n",
                   pid,
                   forked,
                   tids.size(),
                   tid,
                   collection.task(tid)->size(),
                   running);
            while ((pid = waitpid(-1,
                                  nullptr,
                                  running < max_processes ? WNOHANG : 0)) > 0)
            {
                // TODO: handle EINTR
                --running;
                print_progress(forked, (forked-running), tids.size());
                SAT_LOG(1, "waited pid %u, running: %u\n", pid, running);
            }
            break;
        }
    }
    while (running > 0) {
        pid_t pid = wait(nullptr);
        // TODO: handle EINTR
        --running;
        print_progress(forked, (forked-running), tids.size());
        SAT_LOG(1, "waited pid %u, running: %u\n", pid, running);
    }
    printf("\n");

child_exit:

    exit(EXIT_SUCCESS);


    // create an execution model and run it
//    ipt_model model(collection, host_filesystem, system_map_path);
//    auto tids = collection.tids();
//    for (auto tid : tids) {
//        model.run(tid);
//    }


} // main()
