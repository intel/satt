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
#ifndef SAT_STP_PARSER_H
#define SAT_STP_PARSER_H

#include <sys/types.h>
#include <memory>

namespace sat {

    using namespace std;

    const int BUFFER_SIZE = 100;

    enum {
        CMD_NULL        = 0,
        CMD_MASTER,     /* 1 */
        CMD_OVERFLOW,   /* 2 */
        CMD_CHANNEL,    /* 3 */
        CMD_D8,         /* 4 */
        CMD_D16,        /* 5 */
        CMD_D32,        /* 6 */
        CMD_D64,        /* 7 */
        CMD_D8TS,       /* 8 */
        CMD_D16TS,      /* 9 */
        CMD_D32TS,      /* a */
        CMD_D64TS       /* b */
    };


    class stp_parser_input {
    public:
        virtual bool read(unsigned size, void* buffer,bool consume_data) = 0;
        virtual bool read(unsigned size, void* buffer) = 0;
        virtual bool peak(unsigned size, void* buffer) = 0;
        virtual bool consume(unsigned size)            = 0;
        virtual bool eof()                             = 0;
        virtual bool bad()                             = 0;
        virtual unsigned long get_offset()             = 0;
    };

    class stp_parser_output {
    public:
        virtual bool overflow(unsigned char master, unsigned char channel, unsigned char count) = 0;
        virtual bool write_8(void*  buf, unsigned char master, unsigned char channel) = 0;
        virtual bool write_16(void* buf, unsigned char master, unsigned char channel) = 0;
        virtual bool write_32(void* buf, unsigned char master, unsigned char channel) = 0;
        virtual bool write_64(void* buf, unsigned char master, unsigned char channel) = 0;
    };

    class stp_parser {
    public:
        stp_parser(shared_ptr<stp_parser_input>  input,
                   shared_ptr<stp_parser_output>  output,
                    bool debug,
                    bool m_only);

        bool parse();
        bool end_of_frame(unsigned char);

    private:
        bool parse_message();

        shared_ptr<stp_parser_input>  input_;
        shared_ptr<stp_parser_output>  output_;
        unsigned char current_master;
        unsigned char current_channel;
        bool debug_print;
        bool master_only;
    };
}

#endif
