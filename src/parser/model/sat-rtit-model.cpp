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
#include "sat-log.h"
#include "sat-file-input.h"
#include "sat-system-map.h"
#include "sat-sideband-model.h"
#include "sat-rtit-parser.h"
#include "sat-rtit-workarounds.h"
#include "sat-rtit-pkt-cnt.h"
#include "sat-scheduling-heuristics.h"
#include "sat-disassembler.h"
#include "sat-rtit-instruction.h"
#include "sat-tid.h"
#include "sat-rtit-model.h"
#include "sat-path-mapper.h"
#include "sat-filesystem.h"
#include <memory>
#include <set>
#include <cstdio>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <cinttypes>
#include <sys/stat.h>

using namespace std;

namespace {
    uint64_t         global_initial_tsc                   = 0;
    unsigned         global_rtit_buffer_overflow_count    = 0;
    sat::rtit_offset global_rtit_input_skipped_bytes      = 0;
    FILE*            global_executables_file              = 0;
    FILE*            global_host_executables_file         = 0;
    FILE*            global_symbols_file                  = 0;
#ifndef NO_SILLY_HEURISTICS
    bool             global_use_kernel_address_heuristics = true;
    string           global_kernel_address_heuristics     = "fentry";
#endif
}

namespace sat {

    bool global_return_compression = true;

    // The minimum distance (in tsc ticks) from an rtit overflow to the
    // next context switch. If the context switch is any closer, we take
    // some heuristic action to not miss it.
    // TODO: make it relative to mtc range, e.g. 2 * mtc range
    //       (RTIT_CTL[MTC_Range] ha the information)
    static const uint64_t MIN_OVERFLOW_TO_CS_TICKS = 0x4000;

    typedef map<tid_t, shared_ptr<call_stack>> call_stacks;


    template <class PARENT_POLICY>
    class detect_scheduling_point : public PARENT_POLICY {
    public:
        typedef function<void(rtit_offset)> scheduling_point_callback;

        detect_scheduling_point(
            shared_ptr<rtit_parser_output> discard) :
            PARENT_POLICY(discard),
            context_(),
            need_scheduling_(true)
        {}

        void emit(rtit_parser_output* output)
        {
            if (need_scheduling_) {
                SAT_LOG(0, "--- SCHEDULING CPU %u TSC %" PRIx64 \
                           " RTIT OFFSET %" PRIx64 "\n",
                        context_->cpu_,
                        context_->tsc_.begin,
                        output->parsed_.offset);
                need_scheduling_ = false;
                switch_to_task_at_(output->parsed_.offset);
            }
            PARENT_POLICY::emit(output);
            if (output->parsed_.offset == context_->next_schedule_offset_) {
                SAT_LOG(0, "CPU %u: DETECTED A SCHEDULING POINT" \
                           " TSC %" PRIx64 ", OFFSET %" PRIx64 "\n",
                        context_->cpu_,
                        context_->tsc_.begin,
                        output->parsed_.offset);
                force_scheduling(output);
                // TODO:
                // set context_->tsc_.begin to exact scheduling time
                // so that CPU selection has the correct info
                switch_from_task_at_(output->parsed_.offset +
                                     output->parsed_.size);
            }
        }

        void set_context(context* c)
        {
            context_ = c;
        }

        void set_scheduling_callbacks(scheduling_point_callback switch_from,
                                      scheduling_point_callback switch_to)
        {
            switch_from_task_at_ = switch_from;
            switch_to_task_at_   = switch_to;
        }

        void force_scheduling(rtit_parser_output* output)
        {
            need_scheduling_ = true;
            output->stop_parsing();
        }

    private:
        context*                  context_;
        scheduling_point_callback switch_to_task_at_;
        scheduling_point_callback switch_from_task_at_;
        bool                      need_scheduling_;
    };


    class rtit_model :
        public rtit_parser_output,
        public enable_shared_from_this<rtit_model>
    {
    public:
        rtit_model(unsigned                    cpu,
                   shared_ptr<sideband_model>  sideband,
                   shared_ptr<call_stacks>     stacks)
            // TODO: get rng_ from sideband (first make sideband have it)
            : rtit_parser_(), have_parsable_data_(), context_(cpu),
              rng_(3), sideband_(sideband),
              kernel_start_address_(), stacks_(stacks),
              show_disassembly_(), cbr_file_()
        {
            context_.resolve_relocation_callback =
                [this](rva& target) -> bool
                {
                    return resolve_relocation(target);
                };
        }

        void set_rtit_path(const string& rtit_path)
        {
            // create the RTIT parser
            SAT_LOG(0, "creating RTIT parser\n");
            typedef file_input<rtit_parser_input> rtit_file_input;
            shared_ptr<rtit_file_input> input{new rtit_file_input};
            if (!input->open(rtit_path)) {
                SAT_ERR("COULD NOT OPEN RTIT FILE '%s' FOR PARSING\n",
                        rtit_path.c_str());
                exit(EXIT_FAILURE);
            }

            shared_ptr<rtit_parser_output> output = shared_from_this();

            rtit_parser_        = make_shared<rtit_parser_type>(input, output);
            have_parsable_data_ = true;

            // set the initial state of the model
            SAT_LOG(0, "setting RTIT model initial state\n");
            scheduling_heuristics_ = make_shared<scheduling_heuristics>(
                                         context_.cpu_, rtit_path, sideband_);
            scheduling_heuristics_->apply();

            rtit_offset initial_offset = 0;
            uint64_t    initial_tsc;
            if (!scheduling_heuristics_->get_initial(initial_offset,
                                                     initial_tsc))
            {
                if (!scheduling_heuristics_->get_next_valid_tsc(initial_offset,
                                                                initial_offset,
                                                                initial_tsc))
                {
                    SAT_ERR("COULD NOT GET INITIAL TSC RANGE FROM RTIT FILE '%s'\n",
                            rtit_path.c_str());
                    exit(EXIT_FAILURE); // TODO: be more graceful
                }
            }

            rtit_parser_->policy.set_context(&context_);
            rtit_parser_->policy.set_scheduling_callbacks(
                                     [this](rtit_offset offset){
                                         this->switch_from(offset);
                                     },
                                     [this](rtit_offset offset){
                                         this->switch_to(offset);
                                     });
            rtit_parser_->policy.skip_to_offset(initial_offset);
            context_.tsc_.begin = initial_tsc;
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

        void set_kernel_image_path(const string& path)
        {
            kernel_image_path_ = path;
        }

        void set_host_filesystem(shared_ptr<path_mapper> host_filesystem)
        {
            host_filesystem_ = host_filesystem;
        }

        void set_cbr_file(FILE* cbr_file)
        {
            cbr_file_ = cbr_file;
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

        bool run()
        {
            bool ok = false;

            SAT_LOG(1, "CPU %u: RUNNING MODEL" \
                       " TSC: [%" PRIx64 "..%" PRIx64 ")\n",
                    context_.cpu_, context_.tsc_.begin, context_.tsc_.end);
            if (have_parsable_data_) {
                ok = rtit_parser_->parse(false, true);
                if (!ok || !rtit_parser_->was_stopped()) {
                    have_parsable_data_ = false;
                }
            }

            return ok;
        }


        void  t()
        {
            context_.taken_ = true;
            if (!execute_until_rtit_packet(&instruction::tnt)) {
                stop_parsing();
            }
            SAT_LOG(2, "%08" PRIx64 ":  T\n", parsed_.offset);
        }

        void nt()
        {
            context_.taken_ = false;
            if (!execute_until_rtit_packet(&instruction::tnt)) {
                stop_parsing();
            }
            SAT_LOG(2, "%08" PRIx64 ": NT\n", parsed_.offset);
        }

        void fup_pge()
        {
            context_.pc_   = parsed_.fup.address;
            context_.lost_ = false;

            tid_t dummy_tid;
            if (!scheduling_heuristics_->get_current(
                                             parsed_.offset,
                                             context_.tsc_.begin,
                                             context_.tsc_.end,
                                             dummy_tid,
                                             context_.next_schedule_offset_))
            {
                SAT_LOG(0, "NO TSC FOR FUP.PGE\n");
                rtit_offset valid_offset;
                if (!scheduling_heuristics_->get_next_valid_tsc(
                                                 parsed_.offset,
                                                 valid_offset,
                                                 context_.tsc_.begin))
                {
                    SAT_LOG(0, "NO MORE VALID DATA, SKIPPING TO END\n");
                    context_.tsc_.begin = context_.tsc_.end;
                    valid_offset = rtit_offset_max;
                }
                SAT_LOG(0, "SKIPPING TO NEXT VALID TSC %" PRIx64 " AT OFFSET %" PRIx64 "\n",
                        context_.tsc_.begin, valid_offset);
                rtit_parser_->policy.skip_to_offset(valid_offset);
            } else {
                if (dummy_tid != context_.tid_) {
                    SAT_ERR("TID CHANGED %u -> %u FOR FUP.PGE AT %" PRIx64 "\n",
                            context_.tid_,
                            dummy_tid,
                            parsed_.offset);
                    exit(EXIT_FAILURE);
                }
            }

            if (context_.next_schedule_offset_ != 0) {
                SAT_LOG(0, "EXPECTING TO SCHEDULE AT %" PRIx64 "\n",
                        context_.next_schedule_offset_);
            }

            SAT_LOG(1, "%08" PRIx64 ": FUP.PGE %" PRIx64 " (%s)\n",
                    parsed_.offset,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());
        }

        void fup_pgd()
        {
            context_.pc_ = parsed_.fup.address;
            context_.get_lost();

            SAT_LOG(1, "%08" PRIx64 ": FUP.PGD %" PRIx64 " (%s)\n",
                    parsed_.offset,
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
                    parsed_.offset, parsed_.fup.address);
            SAT_LOG(1, "TSC [%" PRIx64 " .. %" PRIx64 ")\n",
                    context_.tsc_.begin, context_.tsc_.end);

            // always schedule if there is an overflow
            rtit_parser_->policy.force_scheduling(this);

            if (!scheduling_heuristics_->get_tsc(parsed_.offset,
                                                 context_.tsc_.begin,
                                                 context_.tsc_.end))
            {
                SAT_LOG(0, "NO TSC FOR THE OVERFLOW\n");
                rtit_offset valid_offset;
                if (!scheduling_heuristics_->get_next_valid_tsc(
                                                 parsed_.offset,
                                                 valid_offset,
                                                 context_.tsc_.begin))
                {
                    SAT_LOG(0, "NO MORE VALID DATA, SKIPPING TO END\n");
                    context_.tsc_.begin = context_.tsc_.end;
                    valid_offset = rtit_offset_max;
                }
                SAT_LOG(0, "SKIPPING TO NEXT VALID TSC %" PRIx64 " AT OFFSET %" PRIx64 "\n",
                        context_.tsc_.begin, valid_offset);
                rtit_parser_->policy.skip_to_offset(valid_offset);
            }
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
                    parsed_.offset,
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
                    parsed_.offset,
                    parsed_.fup.address,
                    get_location(parsed_.fup.address).c_str());
        }

        void sts()
        {
            uint64_t begin = context_.tsc_.begin;

            tid_t dummy_tid;
            if (!scheduling_heuristics_->get_current(
                                             parsed_.offset,
                                             context_.tsc_.begin,
                                             context_.tsc_.end,
                                             dummy_tid,
                                             context_.next_schedule_offset_))
            {
                SAT_ERR("COULD NOT GET TSC FOR STS AT %" PRIx64 "\n",
                        parsed_.offset);
                exit(EXIT_FAILURE);
            }


            if (context_.next_schedule_offset_ != 0) {
                SAT_LOG(0, "EXPECTING TO SCHEDULE AT %" PRIx64 "\n",
                        context_.next_schedule_offset_);
            }

            if (dummy_tid != context_.tid_) {
                SAT_ERR("TID CHANGED %u -> %u FOR STS AT %" PRIx64 "\n",
                        context_.tid_,
                        dummy_tid,
                        parsed_.offset);
                exit(EXIT_FAILURE);
            }

            if (begin > context_.tsc_.begin) {
                SAT_ERR("on CPU %u @ %08" PRIx64 ": STS %" PRIu64 " steps back in time (%" PRIx64 " -> %" PRIx64 ")\n",
                       context_.cpu_, parsed_.offset, context_.tsc_.begin - begin, context_.tsc_.begin, begin);
                exit(EXIT_FAILURE);
            }

            if (cbr_file_) {
                fprintf(cbr_file_,
                        "%" PRIu64 "|%u|%u|%u\n",
                        context_.tsc_.begin - global_initial_tsc,
                        context_.cpu_,
                        parsed_.sts.acbr,
                        parsed_.sts.ecbr);
            }

            SAT_LOG(1, "%08" PRIx64 ": STS %u, %u, %" PRIx64 " -> %" PRIx64 "..%" PRIx64 "\n",
                    parsed_.offset,
                    parsed_.sts.acbr,
                    parsed_.sts.ecbr,
                    parsed_.sts.tsc,
                    context_.tsc_.begin,
                    context_.tsc_.end);
        }

        void mtc()
        {
            tid_t dummy_tid;
            if (!scheduling_heuristics_->get_current(
                                             parsed_.offset,
                                             context_.tsc_.begin,
                                             context_.tsc_.end,
                                             dummy_tid,
                                             context_.next_schedule_offset_))
            {
                SAT_LOG(0, "NO TSC FOR MTC\n");
                rtit_offset valid_offset;
                if (!scheduling_heuristics_->get_next_valid_tsc(
                                                 parsed_.offset,
                                                 valid_offset,
                                                 context_.tsc_.begin))
                {
                    SAT_LOG(0, "NO MORE VALID DATA, SKIPPING TO END\n");
                    context_.tsc_.begin = context_.tsc_.end;
                    valid_offset = rtit_offset_max;
                }
                SAT_LOG(0, "SKIPPING TO NEXT VALID TSC %" PRIx64 " AT OFFSET %" PRIx64 "\n",
                        context_.tsc_.begin, valid_offset);
                rtit_parser_->policy.skip_to_offset(valid_offset);
            } else {
                if (dummy_tid != context_.tid_) {
                    SAT_ERR("TID CHANGED %u -> %u FOR MTC AT %" PRIx64 "\n",
                            context_.tid_,
                            dummy_tid,
                            parsed_.offset);
                    exit(EXIT_FAILURE);
                }
            }

            if (context_.next_schedule_offset_ != 0) {
                SAT_LOG(0, "EXPECTING TO SCHEDULE AT %" PRIx64 "\n",
                        context_.next_schedule_offset_);
            }

            SAT_LOG(1, "%08" PRIx64 ": MTC %u, %u -> TSC %" PRIx64 "..%" PRIx64 "\n",
                    parsed_.offset, parsed_.mtc.rng, parsed_.mtc.tsc,
                    context_.tsc_.begin, context_.tsc_.end);
        }

        void pip()
        {
            SAT_LOG(1, "%08" PRIx64 ", CPU %u [%" PRIx64 "..%" PRIx64 "]: PIP %" PRIx64 "\n",
                    parsed_.offset,
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
                    parsed_.offset);
            //context_.last_call_nlip_ = 0;
        }

        void warning(rtit_offset offset, REASON type, const char* text)
        {
            if (type == BROKEN_OVERFLOW) {
                context_.get_lost();
                ++global_rtit_buffer_overflow_count;

                output_lost("overflow (compressed)",
                            global_rtit_buffer_overflow_count);
            }

            printf("@ ! iWARNING: RTIT issue on cpu %u @ %08" PRIx64 ": %s\n",
                   context_.cpu_, offset, text);
        }

        void skip(rtit_offset offset, rtit_offset count)
        {
            context_.get_lost();

            global_rtit_input_skipped_bytes += count;

            output_lost("skip", global_rtit_input_skipped_bytes);

            SAT_LOG(1, "%" PRIx64 ": SKIP %" PRIx64 "\n", offset, count);

            // always schedule if we skip
            rtit_parser_->policy.force_scheduling(this);

            if (!scheduling_heuristics_->get_tsc(offset + count,
                                                 context_.tsc_.begin,
                                                 context_.tsc_.end))
            {
                SAT_LOG(0, "NO TSC FOR THE SKIP\n");
                rtit_offset next_offset;
                if (!scheduling_heuristics_->get_next_valid_tsc(
                                                 offset + count,
                                                 next_offset,
                                                 context_.tsc_.begin))
                {
                    SAT_LOG(0, "NO MORE TSCS, SKIPPING TO END\n");
                    context_.tsc_.begin = context_.tsc_.end;
                    next_offset         = rtit_offset_max;
                }
                SAT_LOG(0, "SKIPPING TO NEXT VALID TSC %" PRIx64 " AT OFFSET %" PRIx64 "\n",
                        context_.tsc_.begin, next_offset);
                rtit_parser_->policy.skip_to_offset(next_offset);
            }
        }

    private:
        bool get_instruction_iterator(tid_t                  tid,
                                      rva                    address,
                                      uint64_t               tsc,
                                      instruction_iterator*& ii,
                                      string&                target_path,
                                      string&                host_path,
                                      string&                symbols_path,
                                      rva&                   start)
        {
            //printf("getting an instruction iterator for %" PRIx64 "\n", address);
            bool got_it = false;

            string   name;
            unsigned dummy;

            // get the executable file and its load address
            if (kernel_map_ &&
                kernel_map_->get_function(address, name, dummy))
            {
                // it is a kernel address
                target_path = "/vmlinux";
                host_path   = kernel_image_path_;
                symbols_path = host_path;
                start       = kernel_start_address_;
            } else if (sideband_->get_target_path(tid, address, tsc,
                                                  host_path, start))
            {
                // it is a userspace address
                target_path = host_path;
                symbols_path = host_path;
                host_filesystem_->find_file(target_path, host_path, symbols_path);
            } else {
                // it could be a driver or something else dynamic
                // TODO
            }

            // disassemble the instruction in the executable file
            if (host_path != "") {
                shared_ptr<disassembler> d(disassembler::obtain(host_path, symbols_path, start));
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
                }
            }

            return got_it;
        }

        unsigned symbol_id(const string& symbol)
        {
            static symbol_table symbols;
            unsigned id;

            if (symbols.get_new_id(symbol, id) && global_symbols_file) {
                fprintf(global_symbols_file,
                        "%u|%s\n",
                        id, demangle(symbol).c_str());
            }

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
                if (!get_instruction_iterator(context_.tid_,
                                              context_.pc_,
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
                        static symbol_table modules;

                        context_.output_instructions();
                        unsigned id;
                        if (modules.get_new_id(target_path, id)) {
                            // we entered a module we have not been in before;
                            // store its ID to a file
                            if (global_executables_file) {
                                fprintf(global_executables_file,
                                        "%u|%s\n",
                                        id, target_path.c_str());
                            }
                            if (global_host_executables_file) {
                                fprintf(global_host_executables_file,
                                        "%u|%s\n",
                                        id, host_path.c_str());
                            }
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

        bool switch_from(rtit_offset offset)
        {
            tid_t old_tid = context_.tid_;
            if (!scheduling_heuristics_->get_current(
                                             offset,
                                             context_.tsc_.begin,
                                             context_.tsc_.end,
                                             context_.tid_,
                                             context_.next_schedule_offset_))
            {
                SAT_LOG(0, "COULD NOT GET TSC FOR TASK AT %" PRIx64 "\n", offset);
                rtit_offset next_offset;
                if (!scheduling_heuristics_->get_next_valid_tsc(
                                                 offset,
                                                 next_offset,
                                                 context_.tsc_.begin))
                {
                    SAT_LOG(0, "NO MORE TSCS, SKIPPING TO END\n");
                    context_.tsc_.begin = context_.tsc_.end;
                    next_offset         = rtit_offset_max;
                }
                rtit_parser_->policy.skip_to_offset(next_offset);
            }
            pid_t thread_id;
            tid_get_thread_id(context_.tid_, thread_id);
            if (context_.tid_ == old_tid) {
                SAT_LOG(1, "CPU %u: SWITCH_FROM: SAME TASK %d; NO SCHEDULING DONE\n",
                        context_.cpu_,
                        thread_id);
            } else {
                pid_t old_thread_id;
                tid_get_thread_id(old_tid, old_thread_id);
                SAT_LOG(1, "CPU %u: SWITCH_FROM TASK %d -> %d\n",
                        context_.cpu_, old_thread_id, thread_id);
            }

            SAT_LOG(0, "CPU %u: NEXT TASKS FIRST TSC SLOT WILL BE [%" PRIx64 " .. %" PRIx64 ")\n",
                    context_.cpu_,
                    context_.tsc_.begin,
                    context_.tsc_.end);

            return true; // TODO: make void?
        }

        bool switch_to(rtit_offset offset)
        {
            bool switching_stack = false;

            // we only know two things about the task to switch to:
            // 1. the RTIT offset it runs from (offset)
            // 2. the time it gets scheduled in (context_.tsc_.begin)

            SAT_LOG(2, "CPU %u: SWITCH_TO" \
                       " TSC %" PRIx64 " RTIT OFFSET %" PRIx64 "\n",
                       context_.cpu_, context_.tsc_.begin, offset);

            // get more information using what we have
            tid_t old_tid = context_.tid_;
            scheduling_heuristics_->get_current(offset,
                                                context_.tsc_.begin,
                                                context_.tsc_.end,
                                                context_.tid_,
                                                context_.next_schedule_offset_);

            // use the scheduling timestamp from sideband to improve accuracy
            // TODO

            pid_t thread_id;
            tid_get_thread_id(context_.tid_, thread_id);
            if (context_.tid_ == old_tid) {
                SAT_LOG(1, "CPU %u:" \
                           " SWITCH_TO: SAME TASK %d; NO SCHEDULING DONE\n",
                        context_.cpu_, thread_id);
            } else {
                pid_t old_thread_id;
                tid_get_thread_id(old_tid, old_thread_id);
                SAT_LOG(1, "CPU %u: SWITCH_TO TASK %d -> %d\n",
                        context_.cpu_, old_thread_id, thread_id);
            }
            switching_stack = switch_stack();
            if (!switching_stack) {
                SAT_LOG(1, "STACK NOT SWITCHED\n");
            } else {
                // Since proper heuristics for determining exact
                // scheduling points have not yet been found, the
                // stack tends to get messed up. Typically, there
                // are trace calls left on the stack, due to
                // getting either the scheduling point or (worse) the
                // scheduled task wrong, and the stack keeps growing.
                // As a crude -- hopefully temporary -- solution
                // curb stack growth by dropping the stack at
                // context switch.

                // !!! SAND hack, to be defined !!!
                //context_.call_stack_->clear();
            }

            if (context_.next_schedule_offset_) {
                SAT_LOG(1, "NEXT CONTEXT SWITCH AT %" PRIx64 "\n",
                        context_.next_schedule_offset_);
            } else {
                SAT_LOG(1, "NO CONTEXT SWITCH AT SIGHT\n");
            }

            return switching_stack;
        }

        bool switch_stack()
        {
            bool switching_stack = false;

            context_.output_instructions();

            auto i = stacks_->find(context_.tid_);
            if (i == stacks_->end()) {
                context_.call_stack_ = make_shared<call_stack>();
                stacks_->insert({context_.tid_, context_.call_stack_});
                switching_stack = true;
            } else {
                if (context_.call_stack_ != i->second) {
                    context_.call_stack_ = i->second;
                    switching_stack = true;
                }
            }

            context_.output_thread(context_.tid_);

            return switching_stack;
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
            } else if (sideband_->get_target_path(context_.tid_,
                                                  address,
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
            } else if (sideband_->get_target_path(context_.tid_,
                                                  address,
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
            if (!sideband_->get_target_path(context_.tid_,
                                            target,
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

        typedef rtit_parser<
                    postpone_early_mtc<
                    skip_to_offset_on_request<
                    detect_scheduling_point<
                    pkt_cnt<
                    skip_after_overflow_with_compressed_lip>>>>>
                rtit_parser_type;

        shared_ptr<rtit_parser_type>      rtit_parser_;
        shared_ptr<scheduling_heuristics> scheduling_heuristics_;
        bool                              have_parsable_data_;
        context                           context_;
        unsigned                          rng_; // MTC rng
        shared_ptr<sideband_model>        sideband_; // cannot be const
        shared_ptr<const system_map>      kernel_map_;
        string                            kernel_image_path_;
        rva                               kernel_start_address_;
        rva                               kernel_scheduling_address_;
        shared_ptr<path_mapper>           host_filesystem_;
        shared_ptr<call_stacks>           stacks_;

#ifdef FIND_INSTRUCTION_CACHE_BY_PATH
        map<string, instruction_cache*> caches_;
#else // find instruction cache by disassembler address
        map<disassembler*, instruction_cache*> caches_;
#endif
        bool                         show_disassembly_;

        FILE*                        cbr_file_;
    }; // class rtit_model


    typedef vector<shared_ptr<rtit_model>> rtit_models;
    bool select_cpu(const rtit_models& models, unsigned& cpu)
    {
        bool got_one = false;;

        uint64_t smallest_tsc = 0;
        unsigned i            = 0;
        for (auto& model : models) {
            if (model->is_runnable() &&
                (smallest_tsc == 0 || model->tsc() < smallest_tsc))
            {
                cpu     = i;
                got_one = true;
                smallest_tsc = model->tsc();
            }

            ++i;
        }

        return got_one;
    }
}


void usage(const char* name)
{
    printf("Usage: %s [-r <rtit-file>]"
                    " [-s <sideband-file>]"
                    " [-k <kernel-image>]"
                    " [-f <kernel-modules-folder>]"
                    " [-m <System.map>]"
                    " [-t <target-filesystem-root>]"
                    " [-v <vdso-file>]"
                    " [-c <acbr/ecbr-output-file>]"
                    " [-e <list-of-executables-output-file>]"
                    " [-w <stack-low-water-marks-output-file>]"
                    "\n", name);
}

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

int main(int argc, char* argv[])
{
    using namespace sat;

    bool           show_disassembly = false;
    string         kernel_image_path;
    string         kernel_modules_path;
    string         system_map_path;
    vector<string> rtit_paths;
    string         sideband_path("-"); // default to stdin
    string         vdso_path;
    string         cbr_path;     // acbr/ecbr output path
    FILE*          cbr_file = 0; // acbr/ecbr output file
    string         executables_path;
    string         host_executables_path;
    string         symbols_path;
    string         process_table_path;     // thread/process table output path
    FILE*          process_table_file = 0; // thread/process table output file
    string         stack_low_water_marks_path;
    FILE*          stack_low_water_marks_file = 0;
    vector<string> target_filesystem_paths;

    global_use_stderr = false;

    int c;
    // TODO: -o for output path (or - for stdout)
    //       -0 for rtit file of cpu 0 (instead of -r)?
    //       -1 for rtit file of cpu 1 (instead of -r)?
    while ((c = getopt(argc, argv, ":c:dDe:h:k:K:lf:m:n:p:r:Rs:t:v:w:")) != EOF)
    {
        switch (c) {
            case 'c':
                cbr_path = optarg;
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
            case 'k':
                kernel_image_path = optarg;
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
                kernel_modules_path = optarg;
                break;
            case 'm':
                system_map_path = optarg;
                break;
            case 'n':
                symbols_path = optarg;
                break;
            case 'p':
                process_table_path = optarg;
                break;
            case 'r':
                rtit_paths.push_back(optarg);
                break;
            case 'R':
                global_return_compression = false;
                break;
            case 's':
                sideband_path = optarg;
                break;
            case 't':
                target_filesystem_paths.push_back(optarg);
                break;
            case 'v':
                vdso_path = optarg;
                break;
            case 'w':
                stack_low_water_marks_path = optarg;
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

    if (rtit_paths.size() == 0) {
        if (sideband_path == "-") {
            fprintf(stderr, "must specify at least one of -s and -r\n");
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
        rtit_paths.push_back("-"); // default to stdin
    }

    if (cbr_path != "") {
        cbr_file = fopen(cbr_path.c_str(), "wb");
        if (!cbr_file) {
            SAT_ERR("cannot open cbr file for output: '%s'\n",
                    cbr_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (executables_path != "") {
        global_executables_file = fopen(executables_path.c_str(), "wb");
        if (!global_executables_file) {
            SAT_ERR("cannot open executables file for output: '%s'\n",
                    executables_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (host_executables_path != "") {
        global_host_executables_file = fopen(host_executables_path.c_str(), "wb");
        if (!global_host_executables_file) {
            SAT_ERR("cannot open host executables file for output: '%s'\n",
                    host_executables_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (symbols_path != "") {
        global_symbols_file = fopen(symbols_path.c_str(), "wb");
        if (!global_symbols_file) {
            SAT_ERR("cannot open symbols file for output: '%s'\n",
                    symbols_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (process_table_path != "") {
        process_table_file = fopen(process_table_path.c_str(), "wb");
        if (!process_table_file) {
            SAT_ERR("cannot open process table file for output: '%s'\n",
                    process_table_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (target_filesystem_paths.size() == 0) {
        fprintf(stderr, "must specify at least one target fs dir\n");
        exit(EXIT_FAILURE);
    }
    for (const auto& target_path : target_filesystem_paths) {
        if (!is_dir(target_path)) {
            SAT_ERR("cannot open target fs dir: '%s'\n",
                    target_path.c_str());
            exit(EXIT_FAILURE);
        }
    }
    shared_ptr<path_mapper> host_filesystem =
        make_shared<path_mapper>(target_filesystem_paths);

    if (stack_low_water_marks_path != "") {
        stack_low_water_marks_file = fopen(stack_low_water_marks_path.c_str(),
                                           "wb");
        if (!stack_low_water_marks_file) {
            SAT_ERR("cannot open low-water mark file for output: '%s'\n",
                    stack_low_water_marks_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    shared_ptr<system_map> kernel_map;
    if (system_map_path != "") {
        kernel_map = make_shared<system_map>();
        kernel_map->read(system_map_path);
    }

    SAT_LOG(0, "building sideband model\n");
    shared_ptr<sideband_model> sideband{new sideband_model};
    if (kernel_modules_path != "") {
        string dir;
        if (!host_filesystem->find_dir(kernel_modules_path, dir) ||
            !sideband->set_kernel_modules_path(dir))
        {
            SAT_ERR("kernel module directory was not found: '%s'\n",
                    kernel_modules_path.c_str());
            exit(EXIT_FAILURE);
        }
    }
    if (vdso_path != "") {
        sideband->set_vdso_path(vdso_path);
    }
    if (!sideband->build(sideband_path)) {
        SAT_ERR("sideband model building failed\n");
        exit(EXIT_FAILURE);
    }

    shared_ptr<call_stacks> stacks{new call_stacks};
    rtit_models             models;

    unsigned cpu = 0;
    for (auto& path : rtit_paths) {
        shared_ptr<rtit_model> model{new rtit_model(cpu,
                                                    sideband,
                                                    stacks)};
        model->set_rtit_path(path);
        model->set_kernel_image_path(kernel_image_path);
        model->set_host_filesystem(host_filesystem);
        model->set_system_map(kernel_map);
        model->set_disassembly_output(show_disassembly);
        if (cbr_file != 0) {
            model->set_cbr_file(cbr_file);
        }

        models.push_back(model);

        ++cpu;
    }

    if (select_cpu(models, cpu)) {
        global_initial_tsc = models[cpu]->tsc();
        SAT_LOG(0, "GOT INITIAL TSC %" PRIx64 " (%" PRId64 ")\n",
                global_initial_tsc, global_initial_tsc);


        do {
            if (!models[cpu]->run()) {
                printf("@ ! iWARNING: RTIT input for CPU %u ended abruptly\n",
                        cpu);
            }
        } while (select_cpu(models, cpu));
    }

    if (cbr_file != 0) {
        if (fclose(cbr_file) != 0) {
            printf("@ ! iWARNING: error closing cbr output file\n");
        }
    }

    if (process_table_file != 0) {
        tid_iterate([process_table_file, sideband](tid_t    tid,
                                                   pid_t    pid,
                                                   pid_t    thread_id,
                                                   unsigned cpu)
        {
            std::ostringstream thread_name;
            string             t;
            if (sideband->get_thread_name(thread_id, t)) {
                // the thread has its own name; use it
                thread_name << t;
            } else {
                // the thread does not have a name; use the name of the process
                thread_name << sideband->process(pid);
            }

            if (thread_id == 0) {
                // identify which cpu's swapper process this is
                thread_name << "/" << cpu;
            }

            return fprintf(process_table_file,
                           "%u|%u|%u|%s\n",
                           tid,
                           pid,
                           thread_id,
                           thread_name.str().c_str())
                   >= 0;
        });
        fflush(process_table_file);
    }

    if (stack_low_water_marks_file != 0) {
        for (auto &stack : *stacks) {
            fprintf(stack_low_water_marks_file,
                    "%u|%d\n",
                    stack.first, stack.second->low_water_mark());
        }
    }

    report_warnings();

    exit(EXIT_SUCCESS);
}
