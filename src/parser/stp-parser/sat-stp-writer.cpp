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
#include "sat-stp-writer.h"

using namespace sat;
using namespace std;

const int BUFFER_SIZE = 64;

const char *sideband_detection_str = "SATSIDEB";

/* *********************************
 * generic_payload_writer class
 * *********************************/
class generic_payload_writer : public stp_data_writer {
public:

    generic_payload_writer(unsigned char master, unsigned char channel, stp_payload_manager* manager) :
        sideband_detected(false),
        master_id((int) master),
        channel_id((int) channel),
        input_buffer_size(BUFFER_SIZE),
        manager_(manager),
        sat_detect_idx(0)
    {
        stringstream filename_string;

        filename_string << "m" << hex << master_id << "c" << hex << channel_id << ".bin";
        const char *fname = filename_string.str().c_str();
        filename = new char[strlen(fname) + 4];
        strcpy(filename, fname);

        //fprintf(stderr, "file name %s created\n", filename);
        outfile = fopen(filename, "wb");
        if (!outfile) {
            fprintf(stderr, "Can't open file %s !!!\n", filename);
            exit(-1);
        }

        input_buffer.reserve(BUFFER_SIZE);
        writer_type = WRITER_TYPE_GENERIC;
    }

    ~generic_payload_writer()
    {
        fclose(outfile);
        //fprintf(stderr, "Delete file %s...\n", filename);
        if(remove(filename) != 0)
            fprintf(stderr, "Can't delete file %s\n", filename);

        delete filename;
    }

    bool overflow(rtit_offset stp_offset, unsigned char count)
    {
        //fprintf(stderr, "overflow in generic writer stream!!! \n");
        return consume_buffer();
    }

    bool add_8(unsigned char       buf, rtit_offset stp_offset = 0)
    {
        input_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_16(unsigned short     buf, rtit_offset stp_offset = 0)
    {
        input_buffer.push_back(buf>>8);
        input_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_32(unsigned long      buf, rtit_offset stp_offset = 0)
    {
        input_buffer.push_back(buf>>24);
        input_buffer.push_back(buf>>16);
        input_buffer.push_back(buf>>8);
        input_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_64(unsigned long long buf, rtit_offset stp_offset = 0)
    {
        input_buffer.push_back(buf>>56);
        input_buffer.push_back(buf>>48);
        input_buffer.push_back(buf>>40);
        input_buffer.push_back(buf>>32);
        input_buffer.push_back(buf>>24);
        input_buffer.push_back(buf>>16);
        input_buffer.push_back(buf>>8);
        input_buffer.push_back(buf);
        return consume_buffer();
    }
    FILE* get_debug_file() {
        return 0;
    }

    bool sideband_detected;
private:

    bool check_detect_complete(void)
    {
        if(sat_detect_idx >= 7)
        {
            //fprintf(stderr, "SATSIDEB found!\n");
            manager_->sideband_detected(master_id, channel_id);
            return true;
        }
        return false;
    }

    bool consume_buffer()
    {
        int c;
        int idx = 0;
        while(!input_buffer.empty()) {
            c = input_buffer.front();
            input_buffer.erase(input_buffer.begin());
            if (c == sideband_detection_str[sat_detect_idx])
            {
                sat_detect_idx = (sat_detect_idx + 1) % 8;
                if(check_detect_complete())
                    break;
            }
            else
            {
                sat_detect_idx=0;
                if (c == sideband_detection_str[sat_detect_idx])
                    sat_detect_idx++;
            }
            out_buffer[idx++] = c;

        }
        fwrite(out_buffer,1,idx, outfile);
        return true;
    }

    int master_id;
    int channel_id;

    int input_buffer_size;
    vector<unsigned char> input_buffer;

    stp_payload_manager *manager_;

    FILE *outfile;
    int sat_detect_idx;
    unsigned char out_buffer[128];
};


/*************************
 * rtit_input_buffer class
 *************************/

typedef rtit_parser<default_rtit_policy> rtit_parser_type;

typedef struct {
    bool          active;
    unsigned char count;
} overflow_info;



class rtit_input_buffer : public rtit_parser_input {
public:
        rtit_input_buffer(unsigned char channel, bool debug_enabled) :
            core_id(channel),
            read_idx(0),
            rtit_read_offset(0),
            rtit_write_offset(0),
            printing_(true)
        {
            input_buffer.reserve(BUFFER_SIZE);
            stringstream filename_string;
            filename_string << "debug" << (int) core_id << ".txt";
            const char *fname = filename_string.str().c_str();
            char *filename = new char[strlen(fname) + 4];
            strcpy(filename, fname);
            if (debug_enabled) {
                debugfile = fopen(filename, "w");
                if (!debugfile) {
                    fprintf(stderr, "Can't open file %s !!!\n", filename);
                    exit(-1);
                }
            } else {
                debugfile = 0;
            }
        }

        void set_read_printing(bool state)
        {
            printing_ = state;
        }

        bool get_next(unsigned char& byte)
        {
            if (read_idx < (int) input_buffer.size())
            {
                byte = (unsigned char) input_buffer.at(read_idx++);
                rtit_read_offset++;
                if(printing_)
                    if (debugfile)
                        fprintf(debugfile, "  --> %d: %2.2x : 0x%8.8lx (%lu)\n",
                                read_idx-1, byte, (rtit_write_offset - input_buffer.size()) + read_idx-1,
                                rtit_read_offset);
                return true;
            }
            else
            {
                return false;
            }
        }

        bool get_at(unsigned char& byte, int idx)
        {
            if(idx < (int) input_buffer.size())
            {
                byte = (unsigned char) input_buffer.at(idx);
                return true;
            }
            else
            {
                return false;
            }
        }

        bool consume_data(unsigned size)
        {
            read_idx=0;
            if(size)
            {
                if(size > input_buffer.size())
                {
                    size = input_buffer.size();
                }
                input_buffer.erase(input_buffer.begin(),input_buffer.begin() + size);
            }

            return true;
        }

        bool handle_overflow(bool skipping_ongoing)
        {
            if(!skipping_ongoing) {
                input_buffer.clear();
            }
            set_read_printing(false);
            return true;
        }

        bool bad() { return 0; }

        const char* dump(rtit_offset offset)
        {
            return 0;
        }

        bool push_data(unsigned char *buf, int size)
        {
            int idx = 0;
            while (idx < size)
            {
                if(input_buffer.max_size() > input_buffer.size()) {
                    if (debugfile) fprintf(debugfile, "<-- %lu: %2.2x : 0x%8.8lx\n", input_buffer.size(), *(buf + idx), rtit_write_offset + idx);
                    input_buffer.push_back(*(buf + idx));
                }
                else {
                    fprintf(stderr, "RTIT input buffer push error\n");
                    return false;
                }
                idx++;
            }
            read_idx=0;
            rtit_write_offset += size;
            return true;
        }

        rtit_offset get_read_offset()
        {
            return rtit_read_offset;
        }

        rtit_offset get_write_offset()
        {
            return rtit_write_offset;
        }

        FILE *debugfile;
private:
    unsigned char core_id;
    vector<unsigned char> input_buffer;
    int read_idx;
    rtit_offset rtit_read_offset;    // RTIT parser offset. Increments whenever parser reads data, even the same bytes are parsed multiple times.
    rtit_offset rtit_write_offset;  // RTIT buffer write offset.
    bool printing_;
};

class rtit_notifier : public rtit_parser_output {
public:
    rtit_notifier(shared_ptr<rtit_input_buffer> input, unsigned char core_id) :
        input_(input), rtit_buffer_start_offset(0), core_id_(core_id),
        skip_to_psb_(false)
    {
        stringstream filename_stream;
        filename_stream << "cpu" << (int) core_id << ".bin";
        const char *filename = filename_stream.str().c_str();
        outfile = fopen(filename, "wb");

        stp_overflow.active = false;
        stp_overflow.count = 0;
    }

    void psb()
    {
        if (input_->debugfile) fprintf(input_->debugfile, "rtit_parser: psb (%lu)\n", parsed_.pos.offset_);
        skip_to_psb_ = false;
        input_->set_read_printing(true);
    }

    void warning(rtit_pos pos, REASON type, const char* text)
    {
        if(type == ZERO) {
            if (input_->debugfile) fprintf(input_->debugfile, "warning: zero rtit command (%lu)\n", pos.offset_);
        }
        else if (type == RESERVED_PACKET) {
            if (input_->debugfile) fprintf(input_->debugfile, "warning: reserved packet (%lu) => Skip to next PSB\n", pos.offset_);
            /* Not valid RTIT data, discard byte */
            skip_to_psb_ = true;
            input_->set_read_printing(false);
        }
    }

    void skip(rtit_offset offset, rtit_offset count)
        { }

    void set_parser(shared_ptr<rtit_parser_type> parser)
    {
        parser_ = parser;
    }

    bool get_skip_to_psb()
    {
        return skip_to_psb_;
    }

    void force_skip_to_psb()
    {
        skip_to_psb();
    }

    bool handle_overflow(unsigned char count)
    {
        input_->handle_overflow(skip_to_psb_);
        memset(tmp_buf, 0, count);
        input_->push_data(tmp_buf, count);
        skip_to_psb_ = true;
        return true;
    }

    bool handle_result(bool result)
    {
        int idx = 0;
        rtit_offset offset_ = 0;

        if(parsed_.pos.offset_ > rtit_buffer_start_offset)
                offset_ = parsed_.pos.offset_ - rtit_buffer_start_offset;

        if (input_->debugfile) fprintf(input_->debugfile, "handle: offset:%lu (%lu - %lu)\n", offset_, parsed_.pos.offset_, rtit_buffer_start_offset);

        while(idx < (int) offset_)
        {
            int count = 0;
            int loop_count = ((offset_-idx) > 0xFF)?0xFF:(offset_-idx);
            while(count < loop_count)
            {
                input_->get_at(tmp_buf[count++], idx);
                idx++;
            }
            fwrite(tmp_buf,1,count,outfile);
        }
        input_->consume_data(idx);
        rtit_buffer_start_offset = input_->get_read_offset();
        return true;
    }

private:
    overflow_info                  stp_overflow;
    shared_ptr<rtit_input_buffer>  input_;
    shared_ptr<rtit_parser_type>   parser_;
    FILE                           *outfile;
    rtit_offset                    rtit_buffer_start_offset;
    unsigned char                  core_id_;
    unsigned char                  tmp_buf[0x1FF];
    bool                           skip_to_psb_;
};


/* *********************************
 * rtit_payload_writer class
 * *********************************/
class rtit_payload_writer : public stp_data_writer {
public:
    rtit_payload_writer(unsigned char channel, bool debug_enabled) :
        core_id(channel), payload_offset(0), debug_print(debug_enabled)
        {
            input =  make_shared<rtit_input_buffer>(core_id, debug_enabled);
            output =  make_shared<rtit_notifier>(input, core_id);
            rtit_parser = make_shared<rtit_parser_type>(input, output);
            output->set_parser(rtit_parser);
            writer_type = WRITER_TYPE_RTIT;
        }

    bool overflow(rtit_offset stp_offset, unsigned char count)
    {
        if (input->debugfile) fprintf(input->debugfile, "0x%8.8lx: core_id:%d overflow (%u)\n", stp_offset, core_id, count);
        return output->handle_overflow(count);
    }

    bool add_8(unsigned char       buf, rtit_offset stp_offset)
    {
        payload_offset+=1;
        debug_print && printf("    cpu%d: %10.10lx", core_id, payload_offset);
        if (input->debugfile) fprintf(input->debugfile, "0x%8.8lx: add_8 (%d): 0x%2.2x\n", stp_offset, core_id, buf);
        input->push_data(&buf, 1);
        return validate_data();
    }

    bool add_16(unsigned short     buf, rtit_offset stp_offset)
    {
        payload_offset+=2;
        debug_print && printf("    cpu%d: %10.10lx", core_id, payload_offset);
        if (input->debugfile) fprintf(input->debugfile, "0x%8.8lx: add_16 (%d): 0x%4.4x\n", stp_offset, core_id, buf);
        input->push_data((unsigned char*)&buf, 2);
        return validate_data();
    }

    bool add_32(unsigned long      buf, rtit_offset stp_offset)
    {
        payload_offset+=4;
        debug_print && printf("    cpu%d: %10.10lx", core_id, payload_offset);
        if (input->debugfile) fprintf(input->debugfile, "0x%8.8lx: add_32 (%d): 0x%8.8lx\n", stp_offset, core_id, buf&0xFFFFFFFF);
        input->push_data((unsigned char*)&buf, 4);
        return validate_data();
    }

    bool add_64(unsigned long long buf, rtit_offset stp_offset)
    {
        payload_offset+=8;
        debug_print && printf("    cpu%d: %10.10lx", core_id, payload_offset);
        if (input->debugfile) fprintf(input->debugfile, "0x%8.8lx: add_64 (%d): 0x%16.16llx\n", stp_offset, core_id, buf);
        input->push_data((unsigned char*)&buf, 8);
        return validate_data();
    }
    FILE* get_debug_file() {
        return input->debugfile;
    }

private:
    bool validate_data()
    {
        if (output->get_skip_to_psb())
            output->force_skip_to_psb();
        bool ret = rtit_parser->parse();
        if (output->get_skip_to_psb())
            if (input->debugfile) fprintf(input->debugfile, "--> Searching PSB\n");
        if (input->debugfile) fprintf(input->debugfile, "rtit parse (%d): %d (off:%lu) skip_to_psb:%s\n",
                core_id, ret, output->parsed_.pos.offset_, output->get_skip_to_psb()?"true":"false");
        ret = output->handle_result(ret);

        return ret;
    }

    unsigned char core_id;
    rtit_offset payload_offset;
    bool debug_print;

    shared_ptr<rtit_input_buffer>  input;
    shared_ptr<rtit_notifier> output;
    shared_ptr<rtit_parser_type> rtit_parser;
};



/* *********************************
 * sideband_writer class
 * *********************************/
class sideband_writer : public stp_data_writer {
private:
    vector<unsigned char> sb_buffer;
    unsigned char out_buffer[1024];
    unsigned sat_msg_size;
    FILE *outfile;

    bool consume_buffer()
    {
        if (!sat_msg_size)
        {
            /* Ignore null's infront of payload data */
            while(sb_buffer.size() && sb_buffer.front() == 0)
                sb_buffer.erase(sb_buffer.begin());

            /* get msg_size */
            if(sb_buffer.size() >= 4)
            {
                for(int i=0; i<4; i++) {
                    sat_msg_size |= sb_buffer.at(i) << (i*8);
                }
            }
        }

        if(sat_msg_size && (sb_buffer.size() >= sat_msg_size))
        {
            for(int i=0; i<(int)sat_msg_size; i++)
                out_buffer[i] = sb_buffer.at(i);
            fwrite(out_buffer,1,sat_msg_size,outfile);
            sb_buffer.erase(sb_buffer.begin(), sb_buffer.begin()+sat_msg_size);
            sat_msg_size = 0;
        }

        return true;
    }


public:
    sideband_writer()
        :sat_msg_size(0)
    {

        const char *fname = "sideband.bin";
        filename = new char[strlen(fname) + 4];
        strcpy(filename, fname);
        outfile = fopen(filename, "wb");

        sb_buffer.reserve(BUFFER_SIZE);
        writer_type = WRITER_TYPE_SIDEBAND;
    }

    bool overflow(rtit_offset stp_offset, unsigned char count)
    {
        fprintf(stderr, "ERROR: overflow in sideband stream!!! \n");
        return consume_buffer();
    }

    bool add_8(unsigned char       buf, rtit_offset stp_offset)
    {
        sb_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_16(unsigned short     buf, rtit_offset stp_offset)
    {
        sb_buffer.push_back(buf>>8);
        sb_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_32(unsigned long      buf, rtit_offset stp_offset)
    {
        sb_buffer.push_back(buf>>24);
        sb_buffer.push_back(buf>>16);
        sb_buffer.push_back(buf>>8);
        sb_buffer.push_back(buf);
        return consume_buffer();
    }

    bool add_64(unsigned long long buf, rtit_offset stp_offset)
    {
        sb_buffer.push_back(buf>>56);
        sb_buffer.push_back(buf>>48);
        sb_buffer.push_back(buf>>40);
        sb_buffer.push_back(buf>>32);
        sb_buffer.push_back(buf>>24);
        sb_buffer.push_back(buf>>16);
        sb_buffer.push_back(buf>>8);
        sb_buffer.push_back(buf);
        return consume_buffer();
    }
    FILE* get_debug_file() {
        return 0;
    }

};




/* *********************************
 * stp_payload_manager functions
 * *********************************/
bool stp_payload_manager::overflow(rtit_offset stp_offset, unsigned char master, unsigned char channel, unsigned char count)
{
    int ret = true;
    stp_data_writer *writer = get_data_writer(master, channel, stp_offset);
    if (writer) {
        ret = writer->overflow(stp_offset, count);
    }
    return ret;
}

bool stp_payload_manager::add_8(unsigned char       buf,  rtit_offset stp_offset, unsigned char master, unsigned char channel)
{
    int ret = true;
    stp_data_writer *writer = get_data_writer(master, channel, stp_offset);
    if (writer) {
        ret = writer->add_8(buf, stp_offset);
        if(sideband_mc>=0)
            switch_to_sideband(stp_offset);
    }
    return ret;
}

bool stp_payload_manager::add_16(unsigned short     buf,  rtit_offset stp_offset, unsigned char master, unsigned char channel)
{
    int ret = true;
    stp_data_writer *writer = get_data_writer(master, channel, stp_offset);
    if (writer) {
        ret = writer->add_16(buf, stp_offset);
        if(sideband_mc>=0)
            switch_to_sideband(stp_offset);
    }
    return ret;
}

bool stp_payload_manager::add_32(unsigned long      buf,  rtit_offset stp_offset, unsigned char master, unsigned char channel)
{
    int ret = true;
    stp_data_writer *writer = get_data_writer(master, channel, stp_offset);
    if (writer) {
        ret = writer->add_32(buf, stp_offset);
        if(sideband_mc>=0)
            switch_to_sideband(stp_offset);
    }
    return ret;
}

bool stp_payload_manager::add_64(unsigned long long buf,  rtit_offset stp_offset, unsigned char master, unsigned char channel)
{
    int ret = true;
    stp_data_writer *writer = get_data_writer(master, channel, stp_offset);
    if (writer) {
        ret = writer->add_64(buf, stp_offset);
        if(sideband_mc>=0)
            switch_to_sideband(stp_offset);
    }
    return ret;
}

stp_data_writer* stp_payload_manager::get_data_writer(unsigned char master, unsigned char channel, rtit_offset stp_offset=0)
{
    unsigned short mc = (master << 8) | channel;
    if(!data_writer_list[mc])
    {
        if(master >= RTIT_DATA_MASTER_START && master <= RTIT_DATA_MASTER_END)
        {
            if (master_only) {
                data_writer_list[mc] = new rtit_payload_writer(master & RTIT_DATA_MASTER_CORE_ID_MASK, debug_);
                if (debug_) printf("\r CPU%u.bin @[0x%lx]         \n", master & RTIT_DATA_MASTER_CORE_ID_MASK, stp_offset);
            }
            else {
                data_writer_list[mc] = new rtit_payload_writer(channel, debug_);
                if (debug_) printf("\r CPU%u.bin @[0x%lx]         \n", channel, stp_offset);
            }
        }
        else if(!master_only || debug_)
        {
            if (debug_) printf("\r Data stream M:%u C:%u @[0x%lx]\n", master, channel, stp_offset);
            data_writer_list[mc] = new generic_payload_writer(master, channel, this);
        }
        else
            data_writer_list[mc] = 0;
    }
    return data_writer_list[mc];
}

void stp_payload_manager::sideband_detected(unsigned char master, unsigned char channel)
{
    sideband_mc = (master << 8) | channel;
}

bool stp_payload_manager::switch_to_sideband(rtit_offset stp_offset)
{
    unsigned short mc = (unsigned short) sideband_mc;
    sideband_mc = -1;
    if (debug_) printf("\r Sideband stream (%u) @[0x%lx]   \n", mc, stp_offset);
    if(!data_writer_list[mc])
    {
        fprintf(stderr, "data writer %x:%x not found!!!\n", (mc >> 8) & 0xFF, (mc & 0xFF));
        exit(-1);
    }
    else
    {
        generic_payload_writer *writer = dynamic_cast<generic_payload_writer*>(data_writer_list[mc]);

        /* create new sideband object */
        data_writer_list[mc] = new sideband_writer();

        /* Remove generic data writer object */
        if (writer->get_type() == WRITER_TYPE_GENERIC)
        {
            delete writer;
        }
        else
        {
            fprintf(stderr, "Unexpected writer type object %d!\n", writer->get_type());
            fprintf(stderr, "Can't remove generic data writer object %s!!!!\n", writer->name());
            exit(-1);
        }
        return true;
    }
}
