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
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <climits>
#include <vector>

using namespace std;

const int DEBUG = 1;

class stp_pattern_search {
public:
    stp_pattern_search() :  debug(DEBUG),
                            offset(0),
                            state(0),
                            idx(0),
                            zero_count(0) {}
    bool find_stp_header_start(int &ret_offset, int mode);
    bool add(void *buf, int size);
    void clear();
    int get_payload(unsigned char *buf);
private:
    void add_pattern(unsigned char c);
    void add_pattern(string str);
    void clear_pattern();
    const char *get_pattern();
    void pattern_broken();
    void master_found(unsigned char c);
    void channel_found(unsigned char c);
    void d8_found(unsigned char c);
    void d16_found(unsigned char c);
    void d32_found(unsigned char c);
    void d64_found(unsigned char c);

    stringstream pattern;

    unsigned char *ptr;
    vector<unsigned char> buffer;
    int debug;
    int offset;
    int state;
    int idx;
    int zero_count;
};
