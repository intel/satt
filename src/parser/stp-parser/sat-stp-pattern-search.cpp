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
#include "sat-stp-pattern-search.h"


bool stp_pattern_search::find_stp_header_start(int &ret_offset, int mode)
{
    bool header_found = false;

    /*
    find stp header pattern.

    return: true|false  - STP header found
    param:
        re_offset   - Offset to STP header start point
        mode        - 0: search 3 zeros before header
                      1: start searching header from the beginning

    states:
    0 = initial state
    1 = at least 3 zeros found, find master, channel or dxx cmd
    2 = master found
    3 = channel found
    4 = d8 found
    5 = d16 found
    6 = d32 found
    7 = d64 found
    */

    unsigned char c;
    if(debug) fprintf(stdout, "search pattern...\n");
    while(idx < (int)buffer.size() && !header_found) {
        c = buffer.at(idx);
        if(debug>=2) fprintf(stdout, ":%x:        (%s)\n", c, get_pattern());
        if(debug>=3) fprintf(stdout, "  state %x:\n", state);
        if(debug>=3) fprintf(stdout, "  --------\n");

        /* don't wait for 3 leading zeros */
        if(mode == 1)
            if(state == 0)
                state = 1;

        switch(state) {
            case 0:
                /* find at least 3 zero */
                if(c == 0x0) {
                    zero_count++;
                    add_pattern(c);
                    if(zero_count >= 3) {
                        if(debug>=3) fprintf(stdout, "  | --> state 1\n");
                        state = 1;
                    }
                }
                else
                    pattern_broken();
                break;
            case 1:
                if(c == 0x0) {
                    if(debug>=3) fprintf(stdout, "  | ignore extra leading zeros\n");
                    break;
                }
                else {
                    offset = idx;
                    if(debug>=3) fprintf(stdout, "  | offset = %d\n", offset);
                    /* find master,channel or dxx cmd */
                    if(c == 0x1) master_found(c);
                    else if(c == 0x3) channel_found(c);
                    else if(c == 0x4 || c == 0x8) d8_found(c);
                    else if(c == 0x5 || c == 0x9) d16_found(c);
                    else if(c == 0x6 || c == 0xa) d32_found(c);
                    else if(c == 0x7 || c == 0xb) d64_found(c);
                    else pattern_broken();
                }
                break;
            case 2:
                /* find channel cmd */
                if(c == 0x3) channel_found(c);
                else pattern_broken();
                break;
            case 3:
                /* find dxx cmd */
                if(c == 0x4 || c == 0x8) d8_found(c);
                else if(c == 0x5 || c == 0x9) d16_found(c);
                else if(c == 0x6 || c == 0xa) d32_found(c);
                else if(c == 0x7 || c == 0xb) d64_found(c);
                else pattern_broken();
                break;
            case 4:
            case 5:
            case 6:
            case 7:
                break;
        }
        if(state > 3) {
            if(debug>=2) fprintf(stdout, "  |HEADER FOUND @ %d\n", offset);
            header_found = true;
        }
        idx++;
        if(debug>=2) fprintf(stdout, "  idx:%d\n", idx);
    }
    if(debug>=2) fprintf(stdout, "\n");
    if(header_found) {
        if(debug) fprintf(stdout, ":STP HEADER FOUND @ %d\n", offset);
        ret_offset = offset;
        return true;
    }
    else {
        if(debug) fprintf(stdout, ":STP HEADER NOT FOUND\n");
        return false;
    }
}


bool stp_pattern_search::add(void *buf, int size)
{
    ptr = (unsigned char *)buf;
    if(debug>=2) fprintf(stdout, "buf:0x%x (0x%llx), size:%d\n", *ptr, (unsigned long long)ptr, size);

    if(debug>=2) fprintf(stdout, "Data: ");
    unsigned char c;
    while(size--) {
        c = *(ptr+size);
        buffer.push_back((c>>4) & 0xF);
        if(debug>=2) fprintf(stdout, "%x ", ((c>>4) & 0xF));
        buffer.push_back(c & 0xF);
        if(debug>=2) fprintf(stdout, "%x ", (c & 0xF));
    }
    if(debug>=2) fprintf(stdout, "\n");
    return buffer.size();
}

void stp_pattern_search::clear()
{
    buffer.clear();
    offset = 0;
    state = 0;
    idx = 0;
    zero_count = 0;
    pattern.str("");
}

int stp_pattern_search::get_payload(unsigned char *buf)
{
    int j = 0;
    int i = offset-1;
    if(!(i%2)) i--;
    while(i>=0)
        buf[j++] = buffer.at(i--) | (buffer.at(i--)<<4);
    return j;
}
void stp_pattern_search::add_pattern(unsigned char c)
{
    pattern << (int) c;
}
void stp_pattern_search::add_pattern(string str)
{
    pattern << str;
}

void stp_pattern_search::clear_pattern()
{
    pattern.str("");
}
const char* stp_pattern_search::get_pattern()
{
    return pattern.str().c_str();
}

void stp_pattern_search::pattern_broken()
{
    if(debug>=3) fprintf(stdout, "  | pattern broken..\n");
    if(debug>=3) fprintf(stdout, "  | --> state 0\n");
    clear_pattern();
    state = 0;
    offset = 0;
    zero_count = 0;
}

void stp_pattern_search::master_found(unsigned char c)
{
    add_pattern(c);
    idx++;
    if(debug>=3) fprintf(stdout, "  | master found\n");
    c = buffer.at(idx++);
    if(debug>=3) fprintf(stdout, " %x:        (%s)\n", c, get_pattern());
    add_pattern("m");
    if(debug>=3) fprintf(stdout, "  |:m:%x\n", c);
    c = buffer.at(idx);
    if(debug>=3) fprintf(stdout, " %x:        (%s)\n", c, get_pattern());
    add_pattern("m");
    if(debug>=3) fprintf(stdout, "  |:m:%x\n", c);
    if(debug>=3) fprintf(stdout, "  | --> state 2\n");
    state = 2;
}
void stp_pattern_search::channel_found(unsigned char c)
{
    add_pattern(c);
    idx++;
    if(debug>=3) fprintf(stdout, "  | channel found\n");
    c = buffer.at(idx++);
    if(debug>=3) fprintf(stdout, " %x:        (%s)\n", c, get_pattern());
    add_pattern("c");
    if(debug>=3) fprintf(stdout, "  |:c:%x\n", c);
    c = buffer.at(idx);
    if(debug>=3) fprintf(stdout, " %x:        (%s)\n", c, get_pattern());
    add_pattern("c");
    if(debug>=3) fprintf(stdout, "  |:c:%x\n", c);
    if(debug>=3) fprintf(stdout, "  | --> state 3\n");
    state = 3;
}
void stp_pattern_search::d8_found(unsigned char c)
{
    add_pattern(c);
    if(debug>=3) fprintf(stdout, "  | d8 found\n");
    add_pattern("xx");
    idx += 2;
    if(debug>=3) fprintf(stdout, "  | --> state 4\n");
    state = 4;
}
void stp_pattern_search::d16_found(unsigned char c)
{
    add_pattern(c);
    if(debug>=3) fprintf(stdout, "  | d16 found\n");
    add_pattern("xxxx");
    idx += 4;
    if(debug>=3) fprintf(stdout, "  | --> state 5\n");
    state = 5;
}
void stp_pattern_search::d32_found(unsigned char c)
{
    add_pattern(c);
    if(debug>=3) fprintf(stdout, "  | d32 found\n");
    add_pattern("xxxxxxxx");
    idx += 8;
    if(debug>=3) fprintf(stdout, "  | --> state 6\n");
    state = 6;
}
void stp_pattern_search::d64_found(unsigned char c)
{
    add_pattern(c);
    if(debug>=3) fprintf(stdout, "  | d64 found\n");
    add_pattern("xxxxxxxxxxxxxxxx");
    idx += 16;
    if(debug>=3) fprintf(stdout, "  | --> state 7\n");
    state = 7;
}



#if 0
/* Example usage */
int main(void)
{
    unsigned long long data = 0xb207000001803007;
    unsigned char rtitdata[32];
    stringstream rtitdata_str;

    stp_pattern_search *parser = new stp_pattern_search();

    parser->add(&data, 8);
    int offset = parser->find_stp_header_start();
	printf("offset: %d\n\n", offset);

    int size = parser->get_payload(rtitdata);
    printf("payload size %d\n\n", size);
    offset = 0;
    printf("data: ");
    while(offset < size) {
        printf("%2.2x ", rtitdata[offset++]);
    }
    printf("\n");
	return 0;
}
#endif
