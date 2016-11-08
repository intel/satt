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
#ifndef SAT_STP_WRITER_H
#define SAT_STP_WRITER_H
#include "../rtit-parser/sat-rtit-parser.h"
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <climits>
#include <vector>

namespace sat {

    using namespace std;

    const int RTIT_DATA_MASTER_START = 0x80;
    const int RTIT_DATA_MASTER_END = 0x8F;
    const int RTIT_DATA_MASTER_CORE_ID_MASK = 0x0F;

    class stp_data_writer {
    public:
        virtual ~stp_data_writer() {;}
        virtual bool overflow(rtit_offset stp_offset, unsigned char count) = 0;
        virtual bool add_8(unsigned char       buf, rtit_offset stp_offset) = 0;
        virtual bool add_16(unsigned short     buf, rtit_offset stp_offset) = 0;
        virtual bool add_32(unsigned long      buf, rtit_offset stp_offset) = 0;
        virtual bool add_64(unsigned long long buf, rtit_offset stp_offset) = 0;
        virtual FILE* get_debug_file() = 0;
        char* name() {return filename;}
        char get_type() {return writer_type;}
    protected:
        char *filename;
        char writer_type;
    };

    class stp_payload_manager {
    public:
        stp_payload_manager(bool m_only, bool debug) :
                    //debug_jn(0),
                    sideband_mc(-1),
                    master_only(m_only),
                    debug_(debug)
                    {}
        bool overflow(rtit_offset stp_offset, unsigned char master, unsigned char channel, unsigned char count);
        bool add_8(unsigned char       buf, rtit_offset stp_offset, unsigned char master, unsigned char channel);
        bool add_16(unsigned short     buf, rtit_offset stp_offset, unsigned char master, unsigned char channel);
        bool add_32(unsigned long      buf, rtit_offset stp_offset, unsigned char master, unsigned char channel);
        bool add_64(unsigned long long buf, rtit_offset stp_offset, unsigned char master, unsigned char channel);
        void sideband_detected(unsigned char master, unsigned char channel);
        void update_stp_offset(rtit_offset offset) { offset_ = offset; }
/*        void print(const char *str, unsigned char master, unsigned char channel)
        {
            FILE *file;
            stp_data_writer *writer = get_data_writer(master, channel, 0);
            file = writer->get_debug_file();
            if(file)
            {
                fprintf(file, str);
            }
        }
       */
    private:
        bool switch_to_sideband(rtit_offset stp_offset);
        stp_data_writer* get_data_writer(unsigned char master, unsigned char channel, rtit_offset stp_offset);
        map <unsigned short, stp_data_writer*> data_writer_list;
        rtit_offset offset_;
        //bool debug_jn;
        int sideband_mc;
        bool master_only;
        bool debug_;
    };

    enum {
        WRITER_TYPE_UNKNOWN = 0,
        WRITER_TYPE_GENERIC,
        WRITER_TYPE_RTIT,
        WRITER_TYPE_SIDEBAND,
        NUMBER_OF_WRITER_TYPES      /* The last writer type */
    };

}

#endif /* SAT_STP_WRITER_H */
