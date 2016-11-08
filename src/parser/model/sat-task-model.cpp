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

// TODO:
// - do we make sure to have a full lip_ when skipping?
// - store LIP at the beginning of block
//   (Do NOT add it to rtit_parser_output::item;
//    pass it via file_block_input::open() instead.
//    NO! Pass it via rtit_parser constructor.)
// - output rtit offset from beginning of file
// - normalize time
// - sh::get_initial()        -> block->tsc_.first
// - sh::get_tsc()            -> th::get_tsc()
// - sh::get_current()        -> th::get_tsc()
// - sh::get_next_valid_tsc() -> th::get_next_valid_tsc()
// - kernel modules path to sideband -> serialize
// - CBR file -> create as a separate processing step
// - stack low water marks ->
// - statistics -> merge after parallel processing
// - module IDs -> merge after parallel processing
// - symbol IDs -> merge after parallel processing
#include "sat-demangle.h"
#include "sat-symbol-table.h"
#include "sat-symbol-table-file.h"
#include "sat-log.h"
#include "sat-file-input.h"
#include "sat-system-map.h"
#include "sat-sideband-model.h"
#include "sat-rtit-parser.h"
#include "sat-rtit-workarounds.h"
#include "sat-rtit-pkt-cnt.h"
#include "sat-rtit-tsc-heuristics.h"
#include "sat-disassembler.h"
#include "sat-rtit-instruction.h"
#include "sat-tid.h"
#include "sat-rtit-model.h"
#include "sat-helper-path-mapper.h"
#include "sat-filesystem.h"
#include "sat-rtit-collection.h"
#include "sat-rtit-block.h"
#include <memory>
#include <set>
#include <cstdio>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <cinttypes>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

using namespace std;

namespace {
    unsigned         global_rtit_buffer_overflow_count    = 0;
    sat::rtit_offset global_rtit_input_skipped_bytes      = 0;
#ifndef NO_SILLY_HEURISTICS
    bool             global_use_kernel_address_heuristics = true;
    string           global_kernel_address_heuristics     = "fentry";
#endif
} // anonymous namespace

namespace sat {

    bool     global_return_compression = true;
    uint64_t global_initial_tsc        = 0;


    class rtit_model :
        public rtit_parser_output,
        public enable_shared_from_this<rtit_model>
    {
    public:
        rtit_model(vector<string>             rtit_paths,
                   shared_ptr<sideband_model> sideband)
            // TODO: get rng_ from sideband (first make sideband have it)
            : rtit_parser_(), have_parsable_data_(), context_(),
              rng_(3),
              rtit_paths_(rtit_paths), sideband_(sideband),
              kernel_start_address_(),
              show_disassembly_(),
              symbols_(), executables_()
        {
            for (auto& path : rtit_paths) {
                auto heuristics = make_shared<tsc_heuristics>();
                heuristics->parse_rtit(path);
                heuristics->apply();
                tsc_heuristics_.push_back(heuristics);
            }

            context_.resolve_relocation_callback =
                [this](rva& target) -> bool
                {
                    return resolve_relocation(target);
                };
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

        void set_system_map(shared_ptr<system_map> kernel_map)
        {
            kernel_map_ = kernel_map;

            if (kernel_map_) {
#ifndef NO_SILLY_HEURISTICS
                if (global_use_kernel_address_heuristics) {
                    if (global_kernel_address_heuristics == "all" ||
                        global_kernel_address_heuristics.find("fentry") !=
                            string::npos)
                    {
                        kernel_map_->get_address(
                                         "mcount",
                                         instruction::mcount_address_);
                        if (!instruction::mcount_address_) {
                            kernel_map_->get_address(
                                             "__fentry__",
                                             instruction::mcount_address_);
                        }
                        SAT_LOG(1, "will skip calls to mcount/__fentry__" \
                                   " at 0x%" PRIx64 "\n",
                                instruction::mcount_address_);
                    }

                    if (global_kernel_address_heuristics == "all" ||
                        global_kernel_address_heuristics.find("cmpxchg") !=
                            string::npos)
                    {
                        kernel_map_->get_address(
                                         "this_cpu_cmpxchg16b_emu",
                                         instruction::cmpxchg_address_);
                        SAT_LOG(1, "will skip calls to " \
                                   "this_cpu_cmpxchg16b_emu at 0x%" \
                                   PRIx64 "\n",
                                instruction::cmpxchg_address_);
                    }

                    if (global_kernel_address_heuristics == "all" ||
                        global_kernel_address_heuristics.find("copy_user") !=
                            string::npos)
                    {
                        kernel_map_->get_address(
                                         "copy_user_generic_unrolled",
                                         instruction::copy_user1_address_);
                        kernel_map_->get_address(
                                         "copy_user_enhanced_fast_string",
                                         instruction::copy_user2_address_);
                        SAT_LOG(1, "will replace calls and jumps to" \
                                   " copy_user_generic_unrolled by calls" \
                                   " to copy_user_enhanced_fast_string: 0x%" \
                                   PRIx64 " -> 0x%" PRIx64 "\n",
                                instruction::copy_user1_address_,
                                instruction::copy_user2_address_);
                    }
#  if 0
                    kernel_map_->get_address("__memcpy",
                    instruction::memcpy_address_);
                    SAT_LOG(1, "will replace __memcpy (0x%" PRIx64 ") body with a ret\n",
                    instruction::memcpy_address_);
                    kernel_map_->get_address("__memset",
                    instruction::memset_address_);
                    SAT_LOG(1, "will replace __memset (0x%" PRIx64 ") body with a ret\n",
                    instruction::memset_address_);
#  endif // 0
                }
#endif // NO_SILLY_HEURISTICS

                //kernel_map_->get_address("_text", kernel_start_address_);
                kernel_map_->get_address("__switch_to",
                                         kernel_scheduling_address_);
            }
        }

        void set_host_filesystem(shared_ptr<path_mapper> host_filesystem)
        {
            host_filesystem_ = host_filesystem;
            if (host_filesystem_) {
                host_filesystem_->find_file("vmlinux", kernel_image_path_, kernel_image_path_);
            }
        }

        void set_disassembly_output(bool show)
        {
            show_disassembly_ = show;
        }

        uint64_t tsc() const
        {
            return context_.tsc_.begin;
        }

        bool is_runnable() const
        {
            return have_parsable_data_;
        }

        bool run(shared_ptr<rtit_block> block)
        {
            bool ok = true;

            get_tsc(block->pos_.first);

            SAT_LOG(0, "opening RTIT file '%s'\n",
                    rtit_paths_[block->cpu_].c_str());
            using rtit_file_input = file_block_input<rtit_parser_input>;
            auto input = make_shared<rtit_file_input>();
            if (!input->open(rtit_paths_[block->cpu_],
                             block->pos_.first.offset_,
                             block->pos_.second.offset_))
            {
                SAT_ERR("COULD NOT OPEN RTIT FILE '%s' FOR PARSING\n",
                        rtit_paths_[block->cpu_].c_str());
                exit(EXIT_FAILURE);
            }

            shared_ptr<rtit_parser_output> output = shared_from_this();

            rtit_parser_ = make_shared<rtit_parser_type>(input,
                                                         output,
                                                         nullptr,
                                                         block->pos_.first.lip_);
            //rtit_parser_->set_lip(block->lip_);
            have_parsable_data_ = true;

            SAT_LOG(0, "RUN TASK %u ON CPU %u\n",
                    context_.tid_,
                    context_.cpu_);
            SAT_LOG(0, "BLOCK TSC:    [%" PRIx64 "..%" PRIx64 "), " \
                                  "[%" PRIx64 "..%" PRIx64 ")\n",
                    block->tsc_.first,
                    block->tsc_.second,
                    block->tsc_.first  - global_initial_tsc,
                    block->tsc_.second - global_initial_tsc);
            SAT_LOG(0, "BLOCK OFFSET: [%" PRIx64 "..%" PRIx64 ")\n",
                    block->pos_.first.offset_,
                    block->pos_.second.offset_);
            if (have_parsable_data_) {
                context_.maybe_output_timestamp();
                ok = rtit_parser_->parse(false, false);
                if (!ok || !rtit_parser_->was_stopped()) {
                    have_parsable_data_ = false;
                }
                context_.maybe_output_timestamp();
            }

            return ok;
        }

        bool run(shared_ptr<rtit_task> task)
        {
            SAT_LOG(0, "using %lu RTIT files\n", rtit_paths_.size());
            bool ok = true;

            context_.tid_ = task->tid();
            // JNippula mobilevisor stack hack
            if (task->name().find("mobilevisor.elf") != string::npos) {
                SAT_LOG(1, "Mobilevisor elf file, set vmm host flag\n");
                task->set_vmm_host();
            }

            task->iterate_blocks([&](shared_ptr<rtit_block> block) {
                context_.cpu_ = block->cpu_;
                switch (block->type_) {
                case rtit_block::RTIT:
                    ok = run(block);
                    break;
                case rtit_block::SCHEDULE_IN:
                    context_.tsc_.begin = block->tsc_.first;
                    context_.tsc_.end   = block->tsc_.second;
                    context_.output_schedule_in();
                    if (task->is_vmm_host()) {
                        SAT_LOG(1, "Mobilevisor schedule_in, clear stack\n");
                        context_.call_stack_->clear();
                    }

                    break;
                case rtit_block::SCHEDULE_OUT:
                    context_.tsc_.begin = block->tsc_.first;
                    context_.tsc_.end   = block->tsc_.second;
                    context_.output_schedule_out();
                    break;
                case rtit_block::BAD:
                    // TODO
                    break;
                } // switch
                return ok;
            });

            return ok;
        }

        int stack_low_water_mark() const
        {
            return context_.call_stack_->low_water_mark();
        }


        void  t()
        {
            context_.taken_ = true;
            if (!execute_until_rtit_packet(&instruction::tnt)) {
                stop_parsing();
            }
            SAT_LOG(2, "%08" PRIx64 ":  T\n", parsed_.pos.offset_);
        }

        void nt()
        {
            context_.taken_ = false;
            if (!execute_until_rtit_packet(&instruction::tnt)) {
                stop_parsing();
            }
            SAT_LOG(2, "%08" PRIx64 ": NT\n", parsed_.pos.offset_);
        }

        void fup_pge()
        {
            context_.pc_   = parsed_.fup.address;
            context_.lost_ = false;

            get_tsc(parsed_.pos);

            SAT_LOG(1, "%08" PRIx64 ": FUP.PGE %" PRIx64 " (%s)\n",
                    parsed_.pos.offset_,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());
        }

        void fup_pgd()
        {
            context_.pc_ = parsed_.fup.address;
            context_.get_lost();

            SAT_LOG(1, "%08" PRIx64 ": FUP.PGD %" PRIx64 " (%s)\n",
                    parsed_.pos.offset_,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());

            // 2013-06-07: it seems that we are sometimes getting garbage
            // between a FUP.PGD and a PSB, so skip until the PSB.
            // (Alas, it seems that sometimes there is quite a lot of
            // data before the PSB, so we might end up skipping a lot.)
            // 2015-01-29: garbage no longer seen; comment out skipping
            //skip_to_psb();
        }

        void fup_buffer_overflow()
        {
            context_.pc_ = parsed_.fup.address;
            context_.get_lost();

            ++global_rtit_buffer_overflow_count;

            output_lost("overflow", global_rtit_buffer_overflow_count);

            SAT_LOG(1, "%08" PRIx64 ": BUFFER OVERFLOW %" PRIx64 "\n",
                    parsed_.pos.offset_, parsed_.fup.address);
            SAT_LOG(1, "TSC [%" PRIx64 " .. %" PRIx64 ")\n",
                    context_.tsc_.begin, context_.tsc_.end);

            get_tsc(parsed_.pos);

            /* Continue normally from the address give in Buffer overflow */
            context_.fup_far_ = 0;
            context_.pc_ = parsed_.fup.address;
            context_.lost_ = false;
        }

        void tip()
        {
            if (!context_.lost_) {
                if (!context_.fup_far_ && !context_.lost_) {
                    context_.tip_ = parsed_.fup.address;
                    if (!execute_until_rtit_packet(&instruction::tip)) {
                        stop_parsing();
                        // TODO: should we return?
                    }
                }
            } else {
                // we were lost and got a tip;
                // it might be a return address, so try to pop it from the stack
                context_.call_stack_->pop(parsed_.fup.address, context_.lost_);
            }

            context_.fup_far_ = 0;
            context_.pc_      = parsed_.fup.address;
            context_.lost_    = false;

            SAT_LOG(1, "%08" PRIx64 ": TIP %" PRIx64 " (%s)\n",
                    parsed_.pos.offset_,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());
        }

        void fup_far()
        {
            context_.fup_far_ = parsed_.fup.address;

            if (parsed_.fup.address != context_.pc_ && !context_.lost_) {
                if (!execute_until_rtit_packet(&instruction::fup_far)) {
                    stop_parsing();
                    // TODO: should we return?
                }
            }

            SAT_LOG(1, "%08" PRIx64 ": FUP.FAR %" PRIx64 " (%s)\n",
                    parsed_.pos.offset_,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());
        }

        void sts()
        {
            uint64_t begin = context_.tsc_.begin;

            get_tsc(parsed_.pos);

            if (begin > context_.tsc_.begin) {
                SAT_ERR("on CPU %u @ %08" PRIx64 ": STS %" PRIu64 " steps back in time (%" PRIx64 " -> %" PRIx64 ")\n",
                       context_.cpu_, parsed_.pos.offset_, context_.tsc_.begin - begin, context_.tsc_.begin, begin);
                exit(EXIT_FAILURE);
            }

            SAT_LOG(1, "%08" PRIx64 ": STS %u, %u, %" PRIx64 " -> %" PRIx64 "..%" PRIx64 "\n",
                    parsed_.pos.offset_,
                    parsed_.sts.acbr,
                    parsed_.sts.ecbr,
                    parsed_.sts.tsc,
                    context_.tsc_.begin,
                    context_.tsc_.end);
        }

        void mtc()
        {
            get_tsc(parsed_.pos);

            SAT_LOG(1, "%08" PRIx64 ": MTC %u, %u -> TSC %" PRIx64 "..%" PRIx64 "\n",
                    parsed_.pos.offset_, parsed_.mtc.rng, parsed_.mtc.tsc,
                    context_.tsc_.begin, context_.tsc_.end);
        }

        void pip()
        {
            SAT_LOG(1, "%08" PRIx64 ", CPU %u [%" PRIx64 "..%" PRIx64 "]: PIP %" PRIx64 "\n",
                    parsed_.pos.offset_,
                    context_.cpu_,
                    context_.tsc_.begin,
                    context_.tsc_.end,
                    parsed_.pip.cr3);
            sideband_->set_cr3(parsed_.pip.cr3,
                               context_.tsc_.begin,
                               context_.tid_);
        }

        void psb()
        {
            SAT_LOG(1, "%08" PRIx64 ": PSB, clearing RTIT_LAST_CALL_NLIP\n",
                    parsed_.pos.offset_);
            //context_.last_call_nlip_ = 0;
        }

        void warning(rtit_pos pos, REASON type, const char* text)
        {
            if (type == BROKEN_OVERFLOW) {
                context_.get_lost();
                ++global_rtit_buffer_overflow_count;

                output_lost("overflow (compressed)",
                            global_rtit_buffer_overflow_count);
            }

            printf("@ ! iWARNING: RTIT issue on cpu %u @ %08" PRIx64 ": %s\n",
                   context_.cpu_, pos.offset_, text);
        }

        void skip(rtit_pos pos, rtit_offset count)
        {
            context_.get_lost();

            global_rtit_input_skipped_bytes += count;

            output_lost("skip", count);

            SAT_LOG(1, "%" PRIx64 ": SKIP %" PRIx64 "\n", pos.offset_, count);

            get_tsc(parsed_.pos);
        }

    private:
        void get_tsc(const rtit_pos& pos)
        {
            tsc_heuristics_[context_.cpu_]->get_tsc(pos,
                                                    context_.tsc_.begin,
                                                    context_.tsc_.end);
            SAT_LOG(1, "TIMESLOT TSC: [%" PRIx64 "..%" PRIx64 "), " \
                                     "[%" PRIx64 "..%" PRIx64 ")\n",
                    context_.tsc_.begin,
                    context_.tsc_.end,
                    context_.tsc_.begin - global_initial_tsc,
                    context_.tsc_.end   - global_initial_tsc);
        }

        bool get_instruction_iterator(rva                    address,
                                      uint64_t               tsc,
                                      instruction_iterator*& ii,
                                      string&                target_path,
                                      string&                host_path,
                                      rva&                   start)
        {
            //printf("getting an instruction iterator for %" PRIx64 "\n", address);
            bool got_it = false;

            string   name;
            unsigned dummy;
            string   sym_path;

            // get the executable file and its load address
            if (kernel_map_ &&
                kernel_map_->get_function(address, name, dummy))
            {
                // it is a kernel address
                target_path = "/vmlinux"; // TODO: can we ditch the leading '/'?
                host_path   = kernel_image_path_;
                start       = kernel_start_address_;
            } else if (sideband_->get_target_path(address, tsc,
                                                  host_path, start))
            {
                // it is a userspace address
                target_path = host_path;

                host_filesystem_->find_file(target_path, host_path, sym_path);
            } else {
                // it could be a driver or something else dynamic
                // TODO
            }

            // disassemble the instruction in the executable file
            if (host_path != "") {
                shared_ptr<disassembler> d(disassembler::obtain(host_path, sym_path, start));
                if (d) {
                    instruction_cache* ic;
#ifdef FIND_INSTRUCTION_CACHE_BY_PATH
                    auto ici = caches_.find(host_path);
                    if (ici == caches_.end()) {
                        ic = new instruction_cache;
                        caches_.insert({host_path, ic});
                    } else {
                        ic = ici->second;
                    }
#else // find instruction cache by disassembler address
                    auto ici = caches_.find(d.get());
                    if (ici == caches_.end()) {
                        ic = new instruction_cache;
                        caches_.insert({d.get(), ic});
                    } else {
                        ic = ici->second;
                    }
#endif

                    //context_.offset_ = start;
                    context_.offset_ = 0;
                    ii = new instruction_iterator(*ic, d, start, address, name);
                    got_it = true;
                } else {
                    SAT_LOG(1, "could not get disassembler for '%s'\n",
                            host_path.c_str());
                }
            } else {
                SAT_LOG(1, "no path for iterator\n");
            }

            return got_it;
        }

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
                       context_.call_stack_->depth(),
                       synthetic_symbol);
            }
            // synthetic instruction count of one to get a unique timestamp
            context_.instruction_count_++;
            context_.output_instructions(synthetic_id);

            // output for the UI to display stats to the user
            printf("@ ! %c %" PRIu64 "\n", synthetic_symbol[0], count);
        }

        bool execute_until_rtit_packet(
                bool (instruction::* execute)(context& c) const)
        {
            bool done_with_packet = false;
            bool same_stack       = true;

            if (context_.call_stack_->depth() == 100) {
                static bool stack_dumped = false;
                if (!stack_dumped) {
                    SAT_LOG(0, "CALL STACK HAS GROWN SUSPICIOUSLY DEEP:\n");
                    context_.call_stack_->iterate([this](rva return_address) {
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

                sideband_->adjust_for_hooks(context_.pc_);

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
                        if (show_disassembly_) {
                            printf("@ d %u %d %u@%" PRIx64 " (%s)\n",
                                   context_.cpu_,
                                   context_.call_stack_->depth(),
                                   id,
                                   load_address,
                                   target_path.c_str());
                        }
                    }

                    context_.entry_id_ = entry_id;
                }

#if 0 // this is too dangerous to do; there might be an MTC nearby
                // see if we are set up for scheduling
                // (i.e. we hit __switch_to)
                if (context_.must_schedule_at_ &&
                    context_.pc_ == kernel_scheduling_address_)
                {
                    same_stack = !switch_task();
                }
#endif

                if (show_disassembly_ && !done_with_packet) {
                    printf("@ d %u %d -> %s\n",
                           context_.cpu_,
                           context_.call_stack_->depth(),
                           get_location(context_.pc_).c_str());
                }
                rva next_address;
                rva entry_pc;
                rva previous_pc;

                entry_pc = context_.pc_;
                do {
                    const instruction* i = ii->next(); // next() must be lazy!
                    ++context_.instruction_count_;

                    if (show_disassembly_) {
                        printf("@ d %u %d %" PRIx64 ": %s\n",
                               context_.cpu_,
                               context_.call_stack_->depth(),
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
                           context_.call_stack_->depth(),
                           get_location(context_.pc_).c_str());
                }
#endif
                if (!done_with_packet && !context_.lost_ &&
                    entry_pc <= context_.pc_ && context_.pc_ <= previous_pc)
                {
                    printf("@ ! iNON-TERMINATING LOOP DETECTED\n");
                    sleep(1);
                    context_.get_lost();
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

            if (kernel_map_ &&
                kernel_map_->get_function(address, result, offset))
            {
                // it is a kernel address
            } else if (sideband_->get_target_path(address,
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
#if 1
        string get_location(rva address)
        {
            string   result;
            unsigned offset; // TODO: use it or remove it
            string   path;
            string   sym_path;
            rva      start;

            if (kernel_map_ &&
                kernel_map_->get_function(address, result, offset))
            {
                // it is a kernel address
                result = string("KERNEL:") + result;
            } else if (sideband_->get_target_path(address,
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
#endif

        bool resolve_relocation(rva& target)
        {
            bool resolved_it = false;

            SAT_LOG(0, "RESOLVING %" PRIx64 " :|\n", target);
            string target_path;
            rva    start;
            if (!sideband_->get_target_path(target,
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

            sideband_->iterate_target_paths(
                           context_.tid_,
                           context_.tsc_.begin,
                           [&](const string& target_path, rva start) -> bool
                           {
                               SAT_LOG(3, "CONSIDERING %s @ %" PRIx64 "\n",
                                       target_path.c_str(),
                                       start);

                               string host_path;
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

        typedef rtit_parser<
                    postpone_early_mtc<
                    skip_to_offset_on_request<
                    skip_after_overflow_with_compressed_lip>>>
                rtit_parser_type;

        shared_ptr<rtit_parser_type>       rtit_parser_;
        bool                               have_parsable_data_;
        context                            context_;
        unsigned                           rng_; // MTC rng
        vector<string>                     rtit_paths_;
        vector<shared_ptr<tsc_heuristics>> tsc_heuristics_;
        shared_ptr<sideband_model>         sideband_; // cannot be const
        shared_ptr<const system_map>       kernel_map_;
        string                             kernel_image_path_;
        rva                                kernel_start_address_;
        rva                                kernel_scheduling_address_;
        shared_ptr<path_mapper>            host_filesystem_;

#ifdef FIND_INSTRUCTION_CACHE_BY_PATH
        map<string, instruction_cache*> caches_;
#else // find instruction cache by disassembler address
        map<disassembler*, instruction_cache*> caches_;
#endif
        bool                          show_disassembly_;

        file_backed_symbol_table      symbols_;
        file_backed_symbol_table      executables_;
        shared_ptr<symbol_table_file> host_executables_;
    }; // class rtit_model

void report_warnings()
{
    if (global_rtit_buffer_overflow_count) {
        printf("@ ! iWARNING: there were %u RTIT buffer overflows\n",
                global_rtit_buffer_overflow_count);
    }

    if (global_rtit_input_skipped_bytes) {
        printf("@ ! iWARNING: total of %" PRIu64 " bytes of RTIT input were not parsable\n",
                global_rtit_input_skipped_bytes);
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
         shared_ptr<rtit_model>     model,
         shared_ptr<sideband_model> sideband,
         rtit_collection&           collection,
         const string&              output_path_format,
         const string&              symbols_path,
         const string&              executables_path,
         const string&              host_executables_path,
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

    // set up output files for symbols
    if (!model->set_symbol_paths(symbols_path,
                                 executables_path,
                                 host_executables_path))
    {
        SAT_ERR("cannot open symbol paths\n");
        exit(EXIT_FAILURE);
    }

    // obtain the task
    SAT_LOG(0, "picking task ID '%u'\n", tid);
    auto task = collection.task(tid);
    if (!task) {
        SAT_ERR("no such task ID: %u\n", tid);
        exit(EXIT_FAILURE);
    }

    // run the model
    SAT_LOG(0, "running RTIT model\n");
    SAT_LOG(0, "earliest tsc: %lx\n", global_initial_tsc);
    sideband->set_tid_for_target_path_resolution(tid);
    if (!model->run(task)) {
        printf("@ ! iWARNING: RTIT input for task %u ended abruptly\n",
               task->tid());
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


void usage(const char* name)
{
    printf("Usage: %s [-r <rtit-file>]"
                    " [-s <sideband-file>]"
                    " [-k <kernel-image>]"
                    " [-f <kernel-modules-folder>]"
                    " [-m <System.map>]"
                    " [-t <target-filesystem-root>]"
                    " [-v <vdso-file>]"
                    " [-e <list-of-executables-output-file>]"
                    " [-w <stack-low-water-marks-output-file>]"
                    "\n", name);
}

int main(int argc, char* argv[])
{
    using namespace sat;

    bool           show_disassembly = false;
    string         collection_path;
    string         kernel_image_path;
    string         path_mapper_helper_command_format;
    string         path_mapper_cache_dir_path;
    string         system_map_path;
    string         vdso_path;
    string         executables_path;
    string         host_executables_path;
    string         symbols_path;
    unsigned       max_processes = 3; // default parallel processes
    // default path formats
    string         output_path_format = "task%u.model";
    string         stack_low_water_marks_path_format; // no output by default

    global_use_stderr = false;

    int c;
    while ((c = getopt(argc, argv, ":C:dDe:h:k:K:lf:F:m:n:o:P:Rs:t:v:w:")) !=
           EOF)
    {
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
            case 'h':
                host_executables_path = optarg;
                break;
#ifndef NO_SILLY_HEURISTICS
            case 'K':
                global_kernel_address_heuristics = optarg;
                if (global_kernel_address_heuristics == "none") {
                    global_use_kernel_address_heuristics = false;
                } else {
                    global_use_kernel_address_heuristics = true;
                }
                break;
#endif
            case 'l': // log messages also to stderr
                global_use_stderr = true;
                break;
            case 'f':
                path_mapper_helper_command_format = optarg;
                break;
            case 'F':
                path_mapper_cache_dir_path = optarg;
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
            case 'R':
                global_return_compression = false;
                break;
            case 'v':
                vdso_path = optarg;
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

    if (host_executables_path != "") {
        // truncate file
        FILE *host_executables_file = fopen(host_executables_path.c_str(), "wb");
        if (!host_executables_file) {
            SAT_ERR("cannot open host executables file for output: '%s'\n",
                    host_executables_path.c_str());
            exit(EXIT_FAILURE);
        }
        fclose(host_executables_file);
    }

    if (path_mapper_helper_command_format == "") {
        fprintf(stderr, "must specify path mapper command with -f\n");
        exit(EXIT_FAILURE);
    }
    if (path_mapper_cache_dir_path == "") {
        fprintf(stderr, "must specify path mapper cache dir with -F\n");
        exit(EXIT_FAILURE);
    }
    shared_ptr<helper_path_mapper> host_filesystem =
        make_shared<helper_path_mapper>(path_mapper_cache_dir_path,
                                        path_mapper_helper_command_format);
    host_filesystem->find_file("vmlinux", kernel_image_path, kernel_image_path);

    shared_ptr<system_map> kernel_map;
    if (system_map_path != "") {
        kernel_map = make_shared<system_map>();
        kernel_map->read(system_map_path);
    }

    SAT_LOG(0, "deserializing tasks from '%s'\n", collection_path.c_str());
    ifstream collection_stream(collection_path);
    rtit_collection collection(collection_stream);

    global_initial_tsc = collection.earliest_tsc();
    SAT_LOG(0, "earliest tsc: %lx\n", global_initial_tsc);

    SAT_LOG(0, "building sideband model based on '%s'\n",
            collection.sideband_path().c_str());
    shared_ptr<sideband_model> sideband{new sideband_model};
    sideband->set_host_filesystem(host_filesystem);
    if (vdso_path != "") {
        sideband->set_vdso_path(vdso_path);
    }
    if (!sideband->build(collection.sideband_path(), collection.get_vm_sections())) {
        SAT_ERR("sideband model building failed\n");
        exit(EXIT_FAILURE);
    }

    {
        vm_x86_64_list_type regions = collection.get_x86_64_func_regions();
        for (auto& r : regions) {
            rva dummy;
            shared_ptr<disassembler> d(disassembler::obtain(r.second.module,
                                                            r.second.module,
                                                                    dummy));
            d->add_x86_64_region(r.first, r.second.end);
        }
    }

    SAT_LOG(0, "creating RTIT model\n");
    auto model = make_shared<rtit_model>(collection.rtit_paths(),
                                         sideband);
    model->set_host_filesystem(host_filesystem);
    model->set_system_map(kernel_map);
    model->set_disassembly_output(show_disassembly);

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
    SAT_LOG(0, "running RTIT model of %u tasks with %u parallel processes\n",
            collection.tasks(),
            max_processes);
    unsigned forked  = 0;
    unsigned running = 0;
    auto tids = collection.tids_in_decreasing_order_of_rtit_size();
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
                    sideband,
                    collection,
                    output_path_format,
                    symbols_path,
                    executables_path,
                    host_executables_path,
                    stack_low_water_marks_path_format);
                goto child_exit;
            }
            break;
        default:
            // parent
            ++forked;
            ++running;
            SAT_LOG(0, "forked pid %u: %u/%" PRIu64
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
                SAT_LOG(0, "waited pid %u, running: %u\n", pid, running);
            }
            break;
        }
    }
    while (running > 0) {
        pid_t pid = wait(nullptr);
        // TODO: handle EINTR
        --running;
        SAT_LOG(0, "waited pid %u, running: %u\n", pid, running);
    }

    SAT_LOG(0, "done\n");

child_exit:

    exit(EXIT_SUCCESS);
}
