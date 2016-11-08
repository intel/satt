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
#include "sat-sideband-parser.h"
#include <cstdio>
#include <string>

namespace sat {
static bool version_checked = false;

sideband_parser::sideband_parser(shared_ptr<sideband_parser_input>  input,
                                 shared_ptr<sideband_parser_output> output)
    : input_(input),
      output_(output)
{
}

bool sideband_parser::check_version_once(uint32_t *data)
{
    const string parser_version_str = SIDEBAND_VERSION;
    char version_str_[13];
    int file_version;
    int parser_version;
    sat::version_checked = true;

    // check if data contains beginning of version string
    unsigned int i;
    for (i=0; i<sizeof(*data); i++)
        *(version_str_+i)=*((char*)data+i);
    *(version_str_+i)='\0';
    if (!parser_version_str.compare(0, sizeof(*data), version_str_, sizeof(*data))) {
        // data matched with beginning of SIDEBAND_VERSION string
        // do:
        //   * read rest of version string
        //   * compare with parser version
        //   * if parser newer than sideband file, then
        //       - update data with first message
        //   * else
        //       - return false

        std::string string_ = version_str_;
        if (input_->read( sizeof(SIDEBAND_VERSION)-1 - string_.size(),
           (void*)&version_str_[string_.size()] )) {

            sscanf(&version_str_[8], "%d", &file_version);
            sscanf(&SIDEBAND_VERSION[8], "%d", &parser_version);
            if (file_version > parser_version) {
                printf("Sideband parser version (%d) is older than version found from file (%d)\n",
                       parser_version, file_version);
                return false;
            }
            // Read size of first SAT message to data
            input_->read(sizeof(*data), data);
        } else {
            printf("Not enough data to read sideband file version!!\n");
            return false;
        }
    }
    return true;
}

bool sideband_parser::parse()
{
    bool ok;
    sat::version_checked = false;
    do {
        ok = parse_message();
    } while (ok && !input_->eof());

    return ok && !input_->bad();
}

bool sideband_parser::parse_message()
{

    bool ok = true;
    union {
        sat_header            header;
        sat_msg_process       process;
        sat_msg_mmap          mmap;
        sat_msg_init          init;
        sat_msg_munmap        munmap;
        sat_msg_schedule      schedule;
        sat_msg_hook          hook;
        sat_msg_module        module;
        sat_msg_process_abi2  process_abi2;
        sat_msg_mmap_abi2     mmap_abi2;
        sat_msg_munmap_abi2   munmap_abi2;
        sat_msg_hook_abi2     hook_abi2;
        sat_msg_module_abi2   module_abi2;
        sat_msg_hook_abi3     hook_abi3;
        sat_msg_generic       generic;
        sat_msg_codedump      codedump;
        sat_msg_schedule_abi2 schedule_abi2;
        sat_msg_schedule_abi3 schedule_abi3;
        sat_msg_schedule_id   schedule_id;
        sat_msg_init_abi2     init_abi2;
    } message;

    auto size = sizeof(message.header.size);
    if (input_->read(size, &message.header.size)) {
        if (!version_checked) {
            if (!check_version_once(&message.header.size)) {
                ok = false;
            }
        }
        if (message.header.size == 0) {
            ok = false;
            printf("broken header: zero message size\n");
        } else if (message.header.size < sizeof(message.header)) {
            ok = false;
            printf("broken header: message size too short\n");
        } else if (message.header.size > sizeof(message)) {
            ok = false;
            printf("broken header: message size too long\n");
        } else {
            size = message.header.size - size;
            ok = input_->read(size, (&message.header.size) + 1);
        }

        if (ok) {
            switch (message.header.type) {
                case SAT_MSG_PROCESS:
                    output_->process(message.header,
                                     sat_origin(message.process.origin),
                                     message.process.pid,
                                     message.process.ppid,
                                     message.process.tgid,
                                     message.process.pgd,
                                     message.process.name);
                    break;
                case SAT_MSG_MMAP:
                    output_->mmap(message.header,
                                  sat_origin(message.mmap.origin),
                                  message.mmap.pid,
                                  message.mmap.start,
                                  message.mmap.len,
                                  message.mmap.pgoff,
                                  message.mmap.path);
                    break;
                case SAT_MSG_MUNMAP:
                    output_->munmap(message.header,
                                    sat_origin(message.munmap.origin),
                                    message.munmap.pid,
                                    message.munmap.start,
                                    message.munmap.len);
                    break;
                case SAT_MSG_INIT:
                    output_->init(message.header,
                                  message.init.tgid,
                                  message.init.pid,
                                  message.init.tsc_tick,
                                  message.init.fsb_mhz,
                                  0,0,0);
                    break;
                case SAT_MSG_INIT_ABI2:
                    output_->init(message.header,
                                  message.init_abi2.tgid,
                                  message.init_abi2.pid,
                                  message.init_abi2.tsc_tick,
                                  message.init_abi2.fsb_mhz,
                                  message.init_abi2.tma_ratio_tsc,
                                  message.init_abi2.tma_ratio_ctc,
                                  message.init_abi2.mtc_freq);
                    break;
                case SAT_MSG_SCHEDULE:
                    output_->schedule(message.header,
                                      message.schedule.prev_tgid,
                                      message.schedule.prev_pid,
                                      message.schedule.tgid,
                                      message.schedule.pid,
                                      // grab bits [13:0] of RTIT_PKT_CNT
                                      message.schedule.trace_pkt_count & 0x3fff,
                                      // grap bits [16:17] of RTIT_PKT_CNT
                                      (message.schedule.trace_pkt_count >> 16) &
                                      0x3,
                                      0,
                                      0);
                    break;
                case SAT_MSG_HOOK:
                    output_->hook(message.header,
                                  message.hook.org_addr,
                                  message.hook.new_addr,
                                  message.hook.size,
                                  0,
                                  message.hook.name);
                    break;
                case SAT_MSG_MODULE:
                    output_->module(message.header,
                                    message.module.addr,
                                    0,
                                    message.module.name);
                    break;
                case SAT_MSG_PROCESS_ABI2:
                    output_->process(message.header,
                                     sat_origin(message.process_abi2.origin),
                                     message.process_abi2.pid,
                                     message.process_abi2.ppid,
                                     message.process_abi2.tgid,
                                     message.process_abi2.pgd,
                                     message.process_abi2.name);
                    break;
                case SAT_MSG_MMAP_ABI2:
                    output_->mmap(message.header,
                                  sat_origin(message.mmap_abi2.origin),
                                  message.mmap_abi2.tgid,
                                  message.mmap_abi2.start,
                                  message.mmap_abi2.len,
                                  message.mmap_abi2.pgoff,
                                  message.mmap_abi2.path);
                    break;
                case SAT_MSG_MUNMAP_ABI2:
                    output_->munmap(message.header,
                                    sat_origin(message.munmap_abi2.origin),
                                    message.munmap_abi2.tgid,
                                    message.munmap_abi2.start,
                                    message.munmap_abi2.len);
                    break;
                case SAT_MSG_HOOK_ABI2:
                    output_->hook(message.header,
                                  message.hook_abi2.org_addr,
                                  message.hook_abi2.new_addr,
                                  message.hook_abi2.size,
                                  0,
                                  message.hook_abi2.name);
                    break;
                case SAT_MSG_MODULE_ABI2:
                    output_->module(message.header,
                                    message.module_abi2.addr,
                                    message.module_abi2.size,
                                    message.module_abi2.name);
                    break;
                case SAT_MSG_HOOK_ABI3:
                    output_->hook(message.header,
                                  message.hook_abi3.org_addr,
                                  message.hook_abi3.new_addr,
                                  message.hook_abi3.size,
                                  message.hook_abi3.wrapper_addr,
                                  message.hook_abi3.name);
                    break;
                case SAT_MSG_GENERIC:
                    output_->generic(message.header,
                                  message.generic.name,
                                  message.generic.data);
                    break;
                case SAT_MSG_CODEDUMP:
                    output_->codedump(message.header,
                                  message.codedump.addr,
                                  message.codedump.size,
                                  message.codedump.name);
                    break;
                case SAT_MSG_SCHEDULE_ABI2:
                    output_->schedule(message.header,
                                      message.schedule_abi2.prev_tgid,
                                      message.schedule_abi2.prev_pid,
                                      message.schedule_abi2.tgid,
                                      message.schedule_abi2.pid,
                                      // grab bits [13:0] of RTIT_PKT_CNT
                                      message.schedule_abi2.trace_pkt_count & 0x3fff,
                                      // grap bits [16:17] of RTIT_PKT_CNT
                                      (message.schedule_abi2.trace_pkt_count >> 16) &
                                      0x3,
                                      message.schedule_abi2.buff_offset,
                                      0);
                    break;
                case SAT_MSG_SCHEDULE_ABI3:
                    output_->schedule(message.header,
                                      message.schedule_abi3.prev_tgid,
                                      message.schedule_abi3.prev_pid,
                                      message.schedule_abi3.tgid,
                                      message.schedule_abi3.pid,
                                      // grab bits [13:0] of RTIT_PKT_CNT
                                      message.schedule_abi3.trace_pkt_count & 0x3fff,
                                      // grap bits [16:17] of RTIT_PKT_CNT
                                      (message.schedule_abi3.trace_pkt_count >> 16) &
                                      0x3,
                                      message.schedule_abi3.buff_offset,
                                      message.schedule_abi3.schedule_id);
                    break;
                case SAT_MSG_SCHEDULE_ID:
                    output_->schedule_idm(message.header,
                                  message.schedule_id.addr,
                                  message.schedule_id.id);
                    break;
                default:
                    printf("broken header: unknown message type %u\n",
                           message.header.type);
                    ok = false;
            }
        }
    } else {
        ok = input_->eof();
    }

    return ok;
}

}
