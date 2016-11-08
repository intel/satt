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
#ifndef SAT_INPUT_H
#define SAT_INPUT_H

#include "sat-ipt.h"
#include <string>
#include <cstdio>
#include <cinttypes>

namespace sat {

using namespace std;
using namespace sat;

class input_from_stdin
{
public:
    input_from_stdin() : file_(stdin), index_(), beginning_of_packet_() {}

    bool get_next(uint8_t& c)
    {
        int i = fgetc(file_);
        if (i != EOF) {
            c = i;
            ++index_;
            return true;
        } else {
            return false;
        }
    }

    bool get_next(size_t n, uint8_t c[])
    {
        size_t bytes = fread(c, 1, n, file_);
        index_ += bytes;

        return bytes == n;
    }

    bool is_fast_forwarding() { return true; }

    bool bad() { return ferror(file_); }

    void mark_beginning_of_packet() { beginning_of_packet_ = index_; }

    //size_t beginning_of_packet() const { return beginning_of_packet_; }
    size_t beginning_of_packet() const { return absolute_beginning_of_packet(); }
    size_t absolute_beginning_of_packet() const { return start_position_ + beginning_of_packet_; }

    size_t index() const { return index_; }

protected:
    FILE*  file_;
    long   start_position_;
    size_t index_;
private:
    size_t beginning_of_packet_;
}; // input_from_stdin

class input_from_file : public input_from_stdin
{
public:
    ~input_from_file()
    {
        close();
    }

    bool open(const string& path)
    {
        close();
        file_ = fopen(path.c_str(), "r");
        return file_;
    }

    void close()
    {
        if (file_ && file_ != stdin) {
            (void)fclose(file_);
            file_ = 0;
        }
    }

    bool seek(long position)
    {
        printf("start_position_ = %lx\n", position);
        start_position_ = position;
        index_ = 0;
        return fseek(file_, position, SEEK_SET) != -1;
    }

    bool is_fast_forwarding() { return true; }

}; // input_from_file

class input_from_file_block : public input_from_file
{
public:
    bool open(const string& path, ipt_offset begin, ipt_offset end, ipt_offset reset_point)
    {
        bool done = false;

        if (input_from_file::open(path)) {
            if (end >= begin && begin >= reset_point && input_from_file::seek(reset_point)) {
                current_ = reset_point;
                start_   = begin;
                end_     = end;
                done     = true;
                // Print filename
                printf("input_from_file::open('%s') %08lx ... %08lx - %08lx\n", basename(path).c_str(), current_, start_, end_);
            }
        }

        return done;
    }

    bool get_next(uint8_t& c)
    {
        if (input_from_file::file_ &&
            current_ < end_        &&
            input_from_file::get_next(c))
        {
            ++current_;
            return true;
        } else {
            return false;
        }
    }

    bool get_next(size_t n, uint8_t c[])
    {
        if (input_from_file::file_ &&
            current_ + n <= end_   &&
            input_from_file::get_next(n, c))
        {
            current_ += n;
            return true;
        } else {
            return false;
        }
    }

    bool is_fast_forwarding()
    {
        //printf("#is_m? %lx -> %lx\n", current_, start_);
        if (current_ < start_)
            return true;
        else
            return false;
    }

private:
    bool open(const string& path);
    const string basename(const string& s)
    {
        string delim = "/";
        auto offset = s.find_last_of(delim);
        if(offset) {
            return s.substr(offset+1);    
        } else {
            return s;
        }
        
        
    }

    ipt_offset current_;
    ipt_offset start_;
    ipt_offset end_;
}; // input_from_file_block

} // sat

#endif // SAT_INPUT_H
