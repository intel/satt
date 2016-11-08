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
#include "sat-stp-parser.h"
#include "sat-stp-writer.h"
//#include "sat-stp-pattern-search.h"
#include "sat-file-input.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <climits>
#include <vector>
#include <sys/stat.h>

using namespace sat;
using namespace std;

/* ***************************
 *     stp_parser_input
 * ************************** */

const int STATUS_INTERVAL_COUNT = 100000;

class stp_stdin : public stp_parser_input {
private:
    /* 4-bits per array */
    vector<unsigned char> input_buffer;
    unsigned int buffered_bits;
    unsigned long long fileoffset;
    unsigned long long filesize;
    unsigned long long progress_count;
public:
    stp_stdin(unsigned long long size) :
        buffered_bits(0),
        fileoffset(0),
        filesize(size),
        progress_count(0)
    {
        input_buffer.reserve(BUFFER_SIZE * 2 + 20);
        if(filesize) filesize = filesize/100;
    }

    void print_status(void)
    {
        if (filesize) {
            if (fileoffset >= progress_count) {
                printf("\rProgress: %3.u%c   ", (unsigned int) ((fileoffset/8)/filesize), '%');
                progress_count = fileoffset + STATUS_INTERVAL_COUNT;
            }
        }
    }

    bool peak(unsigned size, void* buffer) {
        return read(size, buffer, false);
    }

    bool read(unsigned size, void* buffer)
    {
        return read(size, buffer, true);
    }

    /* size in bits max 64-bit*/
    bool read(unsigned size, void* buffer, bool consume_data)
    {
        unsigned char tmp_buffer[BUFFER_SIZE];
        if ( size > 64 ) return false;
        buffered_bits = input_buffer.size() * 4;

        while ( buffered_bits < size )
        {
            /* Not enough data in input buffer, read more data */
            auto s = fread(&tmp_buffer, 1, BUFFER_SIZE, stdin);

            if (s < 1 || ( buffered_bits + s*CHAR_BIT ) < size) {
                return false; // error: did not get enough data
            }

            for (unsigned int x=0;x<s;x++)
            {
                input_buffer.push_back((tmp_buffer[x] >> 4) & 0x0f);
                input_buffer.push_back(tmp_buffer[x] & 0x0f);
            }
            buffered_bits = input_buffer.size() * 4;

        }

        unsigned send_size = 0;
        unsigned long long msg = 0;
        unsigned idx = 0;
        //fprintf(stderr, "read: s:%u\n", size);

        /* Collect data to be sent to client buffer */
        while(send_size < size)
        {
            /* Not enough data in send package, yet */
            msg = msg | ((unsigned long long)input_buffer.at(idx++) << (size - send_size - 4));
            //fprintf(stderr, "  -- %16.16llx , loc:%u , d:%x , << %d\n", msg, send_size, tmp ,(size - send_size - 4));
            send_size += 4;
        }

        if (consume_data)
        {
            // Remove used data from input buffer
            // TODO this is slow and bad way to do this.
            input_buffer.erase(input_buffer.begin(),input_buffer.begin()+idx);
            fileoffset += send_size;
            print_status();
        }

        // Copy data to client buffer
        memcpy(buffer, &msg, ( ( size - 1) / CHAR_BIT +1));
        return true;
    }

    bool consume(unsigned size)
    {
        buffered_bits = input_buffer.size() * 4;
        if(buffered_bits >= size) {
            input_buffer.erase(input_buffer.begin(),input_buffer.begin()+(size>>2));
            buffered_bits = input_buffer.size() * 4;
            fileoffset += size;
            print_status();
            //cout << hex << ((fileoffset+4)/8)-1 << ": \n";
            return true;
        } else {
            return false;
        }
    }

    unsigned long get_offset() { return ((fileoffset+4)/8)-1; }
    //rtit_offset get_offset() { return ((fileoffset+4)/8)-1; }

    bool eof() { return feof(stdin); }

    bool bad() { return ferror(stdin); }
};


/* ***************************
 *     stp_parser_output
 * ************************** */

class stp_out : public stp_parser_output {
public:
    stp_out(shared_ptr<stp_payload_manager> manager, shared_ptr<stp_stdin> input) :
        data_manager(manager), input_(input)
    {
        //pattern_search = new stp_pattern_search();
    }

    virtual bool overflow(unsigned char master, unsigned char channel, unsigned char count)
    {
        bool ret;
        ret = data_manager->overflow(input_->get_offset(), master, channel, count);
        return ret;
    }

    virtual bool write_8(void* buf, unsigned char master, unsigned char channel)
    {
        bool ret;
        ret = data_manager->add_8(*(unsigned char *)buf, input_->get_offset(), master, channel);
        return ret;
    }

    virtual bool write_16(void* buf, unsigned char master, unsigned char channel)
    {
        bool ret;
        ret = data_manager->add_16(*(unsigned short *)buf, input_->get_offset(), master, channel);
        return ret;
    }

    virtual bool write_32(void* buf, unsigned char master, unsigned char channel)
    {
        bool ret;
        ret = data_manager->add_32(*(unsigned long *)buf, input_->get_offset(), master, channel);
        return ret;
    }

    virtual bool write_64(void* buf, unsigned char master, unsigned char channel)
    {
        bool ret;
        ret = data_manager->add_64(*(unsigned long long *)buf, input_->get_offset(), master, channel);
        return ret;
    }

private:
    //stp_pattern_search      *pattern_search;
    shared_ptr<stp_payload_manager>   data_manager;
    shared_ptr<stp_stdin>   input_;
};



void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s < input_file\n"
            "    Parses raw stp stream to separate files according to master/channel:\n"
            "       - Packets with cpu rtit data master are stored into cpuX.bin files,\n"
            "         where X is a channel number.\n"
            "       - Packets staring with 'SATSIDEB' string are stored into\n"
            "         sideband.bin file.\n"
            "       - Packets with other master/channel pairs are stored into\n"
            "         m[master]c[channel].bin files.\n\n",
            name);
}

/* ***************************
 *            main
 * ************************** */

int main(int argc, char* argv[])
{
    shared_ptr<stp_stdin>      input;
    shared_ptr<stp_out>     output;
    shared_ptr<stp_payload_manager>   data_manager;
    unsigned long long filesize = 0;

    bool debug = false;
    bool master_only = false;


    for (int i = 1; i < argc; ++i) {
        string arg(argv[i]);

        if (arg == "-d") {
            debug = true;
        }
        if (arg == "-m") {
            // stp data stream contains only master_id's, no channel id's provided.
            // Dump out only rtit stream, not other data streams
            master_only = true;
        }
        if (arg == "-s") {
            i++;
            if (i < argc) {
                string filesize_str = argv[i];
                istringstream (filesize_str) >> filesize;
            }
        }
        if (arg == "-h") {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    data_manager = make_shared<stp_payload_manager>(master_only, debug);
    input = make_shared<stp_stdin>(filesize);
    output = make_shared<stp_out>(data_manager, input);

    stp_parser parser(input, output, debug, master_only);
    bool parsing_ok = parser.parse();
    if (parsing_ok) {
        printf("\rProgress: 100%c   \n", '%');
        //printf("OK\n");
        exit(EXIT_SUCCESS);
    } else {
        printf("ERROR\n");
        exit(EXIT_FAILURE);
    }
}
