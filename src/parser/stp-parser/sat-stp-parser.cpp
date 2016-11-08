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
#include <cstdio>


namespace sat {

stp_parser::stp_parser(shared_ptr<stp_parser_input>  input,
                       shared_ptr<stp_parser_output>  output,
                       bool debug,
                       bool m_only)
    : input_(input),
      output_(output),
      current_master(0),
      current_channel(0),
      debug_print(debug),
      master_only(m_only)
{
}

bool stp_parser::parse()
{
    bool ok;
    do {
        ok = parse_message();
    } while (ok && !input_->eof());

    fflush (stdout);
    return ok && !input_->bad();
}

bool stp_parser::end_of_frame(unsigned char cmd)
{
    if (cmd == CMD_MASTER || cmd == CMD_NULL )
    {
        return true;
    }
    return false;
}

bool stp_parser::parse_message()
{

    bool ok = true;
    unsigned char buf;
    unsigned char ts;
    unsigned int  buf_32;
    unsigned long long buf_64;

    // Look for CMD MASTER and Matching Master ID
    if ( input_->read(4, &buf) && buf == CMD_MASTER &&
         input_->read(8, &current_master) /*&& current_master == selected_master*/)
    {
        if(debug_print) printf("%10.10lx: MASTER  (%2.2x)\n",input_->get_offset(),current_master);
        //fprintf(stderr, "MASTER: %x\n", current_master);
        // HW MASTER 16-31
        while (input_->peak(4, &buf) && !end_of_frame(buf))
        {
            input_->read(4, &buf);
            switch(buf)
            {
                case CMD_MASTER:
                    ok = input_->read(8, &buf);
                    if(debug_print) printf("%10.10lx: MASTER   (%2.2x)\n", input_->get_offset(), buf);
                    // Error we should not be in Master at this state
                    //fprintf(stderr,"### Error we should not be in Master at this state\n");
                    break;
                case CMD_OVERFLOW:
                    ok = input_->read(8, &buf);
                    if(debug_print) printf("%10.10lx: OVRF    (%2.2x)\n", input_->get_offset(),buf);
                    output_->overflow(current_master, current_channel, buf);
                    // Error we should not be in Master at this state
                    //fprintf(stderr,"### Overflow occured\n");
                    break;
                case CMD_CHANNEL:
                    ok = input_->read(8, &current_channel);
                    if(debug_print) printf("%10.10lx: Channel (%2.2x)\n", input_->get_offset(), current_channel);
                    break;
                case CMD_D8:
                    ok = input_->read(8, &buf);
                    if(debug_print) printf("%10.10lx: D8      (%2.2x)      ", input_->get_offset(), buf);
                    if (ok) {
                        output_->write_8(&buf, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    break;
                case CMD_D16:
                    ok = input_->read(16, &buf_32);
                    if(debug_print) printf("%10.10lx: D16     (%4.4x)    ", input_->get_offset(), buf_32 & 0xFFFF);
                    if (ok) {
                        output_->write_16(&buf_32, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    break;
                case CMD_D32:
                    ok = input_->read(32, &buf_32);
                    if(debug_print) printf("%10.10lx: D32     (%8.8x)", input_->get_offset(), buf_32);
                    if (ok) {
                        output_->write_32(&buf_32, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    break;
                case CMD_D64:
                    ok = input_->read(64, &buf_64);
                    if(debug_print) printf("%10.10lx: D64     (%16.16llx)", input_->get_offset(), buf_64);
                    if (ok) {
                        output_->write_64(&buf_64, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    break;
                case CMD_D8TS:
                    ok = input_->read(8, &buf);
                    if(debug_print) printf("%10.10lx: D8TS    (%2.2x)      ", input_->get_offset(), buf);
                    if (ok) {
                        output_->write_8(&buf, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    ok = input_->read(8, &ts);
                    break;
                case CMD_D16TS:
                    ok = input_->read(16, &buf_32);
                    if(debug_print) printf("%10.10lx: D16TS   (%4.4x)    ", input_->get_offset(), buf_32 & 0xFFFF);
                    if (ok) {
                        output_->write_16(&buf_32, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    ok = input_->read(8, &ts);
                    break;
                case CMD_D32TS:
                    ok = input_->read(32, &buf_32);
                    if(debug_print) printf("%10.10lx: D32TS   (%8.8x)", input_->get_offset(), buf_32);
                    if (ok) {
                        output_->write_32(&buf_32, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    ok = input_->read(8, &ts);
                    break;
                case CMD_D64TS:
                    ok = input_->read(64, &buf_64);
                    if(debug_print) printf("%10.10lx: D64TS   (%16.16llx)", input_->get_offset(), buf_64);
                    if (ok) {
                        output_->write_64(&buf_64, current_master, current_channel);
                    }
                    if(debug_print) printf("\n");
                    ok = input_->read(8, &ts);
                    break;
                default:
                    // Error
                    if(debug_print) printf("%10.10lx: unknown CMD (%2.2x)\n", input_->get_offset(), buf);
                    //fprintf(stderr,"### error CMD (%#x)\n",buf);
                    current_master = 0;
                    current_channel = 0;
                    break;
            }
        }
        /* End of frame, clear found */
        current_master = 0;
        current_channel = 0;
    }

    return ok;
}

}
