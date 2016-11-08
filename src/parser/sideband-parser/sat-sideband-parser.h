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
#ifndef SAT_SIDEBAND_PARSER_H
#define SAT_SIDEBAND_PARSER_H

#include "sat_payload.h" // TODO: see if we can fix the underscore
#include <sys/types.h>
#include <memory>

namespace sat {

    using namespace std;

    class sideband_parser_input {
    public:
        virtual bool read(unsigned size, void* buffer) = 0;
        virtual bool eof()                             = 0;
        virtual bool bad()                             = 0;
    };


    class sideband_parser_output {
    public:
        virtual void init(const sat_header& header,
                          pid_t             pid,
                          pid_t             tid,
                          unsigned          tsc_tick,
                          unsigned          fsb_mhz,
                          uint32_t          tma_ratio_tsc,
                          uint32_t          tma_ratio_ctc,
                          uint8_t           mtc_freq) {};

        virtual void process(const sat_header& header,
                             sat_origin        origin,
                             pid_t             pid,
                             pid_t             ppid,
                             pid_t             tgid,
                             uint64_t          pgd,
                             const char*       name) {};

        virtual void mmap(const sat_header& header,
                          sat_origin        origin,
                          pid_t             pid,
                          uint64_t          start,
                          uint64_t          len,
                          uint64_t          pgoff,
                          const char*       path) {};

        virtual void munmap(const sat_header& header,
                            sat_origin        origin,
                            pid_t             pid,
                            uint64_t          start,
                            uint64_t          len) {};

        virtual void schedule(const sat_header& header,
                              pid_t             prev_pid,
                              pid_t             prev_tid,
                              pid_t             pid,
                              pid_t             tid,
                              uint32_t          ipt_pkt_count,
                              uint32_t          ipt_pkt_mask,
                              uint64_t          buff_offset,
                              uint8_t           schedule_id) {};

        virtual void hook(const sat_header& header,
                          uint64_t          original_address,
                          uint64_t          new_address,
                          uint64_t          size,
                          uint64_t          wrapper_address,
                          const char*       name) {};

        virtual void module(const sat_header& header,
                            uint64_t          address,
                            uint64_t          size,
                            const char*       name) {};

        virtual void generic(const sat_header& header,
                            const char*       name,
                            const char*       data) {};

        virtual void codedump(const sat_header& header,
                              uint64_t          addr,
                              uint64_t          size,
                              const char*       name) {};

        virtual void schedule_idm(const sat_header& header,
                              uint64_t          addr,
                              uint8_t             id) {};
    };


    class sideband_parser {
    public:
        sideband_parser(shared_ptr<sideband_parser_input>  input,
                        shared_ptr<sideband_parser_output> output);

        bool parse();
        bool check_version_once(uint32_t *data);

    private:
        bool parse_message();

        shared_ptr<sideband_parser_input>  input_;
        shared_ptr<sideband_parser_output> output_;
    };

}

#endif
