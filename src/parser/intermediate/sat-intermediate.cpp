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
#include "sat-tid.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <vector>
#include <limits>
#include <memory>
#include <cinttypes>


using namespace std;
using namespace sat;

namespace {

bool debug      = false;
bool cap        = false; // do not cap values that are larger than db accepts
bool fix_stacks = false; // do not use low-water marks to adjust stack levels

FILE* output_file = 0;

}

namespace sat {

struct output_line
{
    output_line(tid_t tid) :
      type_('u'), tsc_(), cpu_(), call_stack_level_(),
      out_of_thread_(), in_thread_(), instruction_count_(),
      tid_(tid), path_id_(), symbol_id_()
    {}

    void output(int low_water_mark) const
    {
        printf("@cpu %010d ", line_count_);
        output(stdout, low_water_mark);
    }

    typedef enum { NORMAL, PLACEHOLDER } output_type;

    void output(FILE* f, int low_water_mark, output_type type = NORMAL) const
    {
        uint64_t in_thread;
        uint64_t out_of_thread;

        in_thread     = in_thread_;
        out_of_thread = out_of_thread_;

        const int32_t cap_value = numeric_limits<decltype(cap_value)>::max();
        if (cap) {
            // cap values to signed int32
            if (in_thread     > (decltype(in_thread))cap_value) {
                in_thread     = cap_value;
            }
            if (out_of_thread > (decltype(out_of_thread))cap_value) {
                out_of_thread = cap_value;
            }
        }

        if (type_ != 'd') {
            int placeholder_zero_padding;
            if (type == PLACEHOLDER) {
                // use the right amount of zero padding depending of capping
                if (cap) {
                    placeholder_zero_padding =
                        numeric_limits<decltype(cap_value)>::digits10 + 1;
                } else {
                    placeholder_zero_padding =
                        numeric_limits<decltype(in_thread)>::digits10 + 1;
                }
            } else {
                placeholder_zero_padding = 0;
            }

            int stack_level_zero_padding;
            if (fix_stacks) {
                stack_level_zero_padding = 0;
            } else {
                stack_level_zero_padding = 3;
            }

            // output in a format suitable for importing to db:
            //         |time stamp
            //         |           |stack level
            //         |           |    |out of thread
            //         |           |    |             |in thread
            //         |           |    |             |             |instruction count
            //         |           |    |             |             |           |type (either 'c' or 'e')
            //         |           |    |             |             |           |  |cpu #
            //         |           |    |             |             |           |  |  |thread ID
            //         |           |    |             |             |           |  |  |  |module ID
            //         |           |    |             |             |           |  |  |  |  |symbol ID
            fprintf(f, "%" PRIu64 "|%0*d|%0*" PRIu64 "|%0*" PRIu64 "|%" PRIu64 "|%c|%u|%u|%u|%u\n",
                    tsc_,
                    stack_level_zero_padding,
                    call_stack_level_ - low_water_mark,
                    placeholder_zero_padding,
                    out_of_thread,
                    placeholder_zero_padding,
                    in_thread,
                    instruction_count_,
                    type_,
                    cpu_,
                    tid_,
                    path_id_,
                    symbol_id_);
        } else {
            fprintf(f, "        %s\n", rest_.c_str());
#if 0
            uint64_t t      = tsc;
            int      digits = 1;
            while (t /= 10) ++digits;
            fprintf(f, "%*s|%d|d|%u|%u|%u|%u %s\n",
                    digits, "",
                    call_stack_level_ - low_water_mark,
                    cpu_,
                    tid_,
                    path_id_,
                    symbol_id_,
                    rest_.c_str());
            fprintf(f, "[%" PRIu64 "|%0d|%0" PRIu64 "|%0" PRIu64 "|%" PRIu64 "|%c|%u|%u|%u|%u]\n",
                    tsc,
                    call_stack_level_ - low_water_mark,
                    out_of_thread,
                    in_thread,
                    instruction_count_,
                    type_,
                    cpu_,
                    tid_,
                    path_id_,
                    symbol_id_);
#endif
        }
    }

    char     type_; // 'c' or 'e' for call and execute
    uint64_t tsc_;
    unsigned cpu_;
    int      call_stack_level_;
    uint64_t out_of_thread_;
    uint64_t in_thread_;
    uint64_t instruction_count_;
    tid_t    tid_;
    unsigned path_id_;
    unsigned symbol_id_;

    unsigned line_count_;
    fpos_t   output_position_;

    string   rest_;
};


class call_stack
{
public:
    explicit call_stack(int low_water_mark) :
        stack_(),
        level_(),
        low_water_mark_(low_water_mark),
        previous_switch_time_()
    {}

    int level() const
    {
        return level_;
    }

    int low_water_mark() const
    {
        return low_water_mark_;
    }

    void push(output_line& l)
    {
        if (output_file) {
            fgetpos(output_file, &l.output_position_);
            l.output(output_file, low_water_mark_, output_line::PLACEHOLDER);
        }
        stack_.push_back(l);
        level_ = l.call_stack_level_ + 1;
        if (debug) {
            printf("PUSH -> %d: ", level_);
            l.output(low_water_mark_);
        }
    }

    void pop(int level, uint64_t tsc)
    {
        while (level_ > level) {
            if (!stack_.empty()) {
                auto& l = stack_.back();
                level_ = l.call_stack_level_;
                switch_out_line(l, tsc);
                if (debug) {
                    printf("POP -> %d\n", level_);
                    l.output(low_water_mark_);
                }
                if (output_file) {
                    fill_in_output_placeholder(l);
                }
                stack_.pop_back();
                if (level_ == level) {
                    // TODO: output the call
                }
            } else {
                level_ = level;
                // TODO: don't output anything
                if (debug) printf("(POP -> %d)\n", level);
            }
        }
    }

    void switch_out_line(output_line& l, uint64_t tsc)
    {
        if (debug) printf("SWITCH OUT %d %010u tsc: %" PRIu64 ", prev: %" PRIu64 ", l.tsc_: %" PRIu64 "%s\n",
               l.cpu_, l.line_count_, tsc, previous_switch_time_, l.tsc_,
               l.tsc_ > tsc ? " AARGH!" : "");
        if (l.tsc_ < previous_switch_time_) {
            if (previous_switch_time_ < tsc) {
                l.in_thread_ += tsc - previous_switch_time_;
            }
        } else {
            if (l.tsc_ < tsc) {
                l.in_thread_ += tsc - l.tsc_;
            }
        }
    }

    void switch_out(uint64_t tsc)
    {
        for (auto& l : stack_) {
            switch_out_line(l, tsc);
        }
        previous_switch_time_ = tsc;
    }

    void switch_in(uint64_t tsc)
    {
        for (auto& l : stack_) {
            if (debug) printf("SWITCH IN %d %010u tsc: %" PRIu64 ", prev: %" PRIu64 ", l.tsc_: %" PRIu64 "%s\n",
                   l.cpu_, l.line_count_, tsc, previous_switch_time_, l.tsc_,
                   l.tsc_ > tsc ? " AARGH!" : "");
            if (tsc > previous_switch_time_) {
                l.out_of_thread_ += tsc - previous_switch_time_;
            }
        }
        previous_switch_time_ = tsc;
    }

    void flush()
    {
        while (!stack_.empty()) {
            auto& l = stack_.back();
            // TODO: should we cumulate something to in-thread or out-of-thread?
            if (debug) {
                l.output(low_water_mark_);
            }
            if (output_file) {
                fill_in_output_placeholder(l);
            }
            stack_.pop_back();
        }
    }

    void fill_in_output_placeholder(const output_line& l)
    {
        fpos_t end;
        fgetpos(output_file, &end);
        fsetpos(output_file, &l.output_position_);
        l.output(output_file, low_water_mark_, output_line::PLACEHOLDER);
        fsetpos(output_file, &end);
    }

private:
    vector<output_line> stack_;
    int                 level_;
    int                 low_water_mark_;
    uint64_t            previous_switch_time_;
}; // call_stack



class normalizing_output_queue
{
public:
    normalizing_output_queue(int low_water_mark) :
        current_tsc_(), instruction_count_(),
        queue_(), call_stack_(make_shared<call_stack>(low_water_mark))
    {}

    void add(const output_line& l)
    {
        if (l.tsc_ == current_tsc_) {
            instruction_count_ += l.instruction_count_;
        } else {
            flush(l.tsc_);
            current_tsc_ = l.tsc_;
            instruction_count_ = l.instruction_count_;
        }
        queue_.push_back(l);
    }

    void flush(uint64_t tsc)
    {
        if (!tsc) {
            tsc = current_tsc_;
        }

        uint64_t     time_span                = tsc - current_tsc_;
        uint64_t     instruction_span         = instruction_count_;
        uint64_t     accumulated_instructions = 0;
        output_line* previous_line            = 0;

        if (instruction_span) {

            for (auto& l : queue_) {
                l.tsc_ += (time_span * accumulated_instructions +
                           instruction_span / 2)
                        / instruction_span;
                if (previous_line) {
                    if (previous_line->type_ == 'e') {
                        previous_line->in_thread_ =
                            l.tsc_ - previous_line->tsc_;
                    }
                    flush_line(*previous_line);
                }
                previous_line = &l;
                accumulated_instructions += l.instruction_count_;
            }

            if (previous_line) {
                if (previous_line->type_ == 'e') {
                    previous_line->in_thread_ = tsc - previous_line->tsc_;
                }
                flush_line(*previous_line);
            }
        } else {
            for (auto& l : queue_) {
                flush_line(l);
            }
        }

        queue_.clear();
    }

    void switch_in(uint64_t tsc)
    {
        call_stack_->switch_in(tsc);
    }

    void switch_out(uint64_t tsc)
    {
        call_stack_->switch_out(tsc);
    }

private:

    void flush_line(output_line& o)
    {
        o.line_count_ = line_count_;

        if (o.call_stack_level_ < call_stack_->level() && o.type_ != 'd') {
            call_stack_->pop(o.call_stack_level_, o.tsc_);
        }

        if (o.type_ == 'c') {
            call_stack_->push(o);
        } else {
            if (debug) {
                o.output(call_stack_->low_water_mark());
            }
            if (output_file) {
                o.output(output_file, call_stack_->low_water_mark());
            }
        }

        ++line_count_;
    }

    static unsigned        line_count_;

    uint64_t               current_tsc_;
    uint64_t               instruction_count_;
    vector<output_line>    queue_;
    shared_ptr<call_stack> call_stack_;
}; // normalizing_output_queue

unsigned normalizing_output_queue::line_count_ = 0;


bool get_intermediate_line(istream& is, string& type, istringstream& rest)
{
    bool got_it = false;

    string line;
    while (getline(is, line)) {

        if (debug) printf("[%s]\n", line.c_str());

        // discard lines not intended for intermediate processing
        if (line.size() < 2 || line[0] != '@') {
            continue;
        }
        line.erase(0, 2); // erase the intermediate processing marker

        rest.clear();
        //rest.seekg(0); // not needed
        rest.str(line);
        rest >> type;
        if (type == "!") {
            printf("%s\n", line.c_str());
        } else {
            got_it = true;
            break;
        }
    }

    return got_it;
}


class per_cpu_data
{
public:
    per_cpu_data(tid_t tid, int low_water_mark) :
        have_pending_output_line_(), line_(tid), queue_(low_water_mark)
    {
        line_.tid_ = tid;
    }

    void queue_line_for_output()
    {
        queue_.add(line_);
        line_.instruction_count_   = 0;
        have_pending_output_line_ = false;
    }

    void parse(const string&  type, istringstream& rest)
    {
        static uint64_t tsc = 0;

        if (type == "e" || type == "c") { // e for execute; c for call
            if (have_pending_output_line_)
            {
                queue_line_for_output();
            }
            line_.type_ = type[0];
            rest >> line_.call_stack_level_ >> line_.symbol_id_;
            if (type == "e") { // e for execute
                rest >> line_.instruction_count_;
                queue_line_for_output();
            } else {
                line_.instruction_count_ = 1;
                have_pending_output_line_ = true;
            }
        } else if (type == "x") { // x for transfer
            rest >> line_.path_id_;
            if (have_pending_output_line_)
            {
                queue_line_for_output();
            }
        } else if (type == "t") { // t for timestamp
            uint64_t        new_tsc;
            rest >> hex >> new_tsc >> dec;
            if (tsc > new_tsc) {
                printf("WARNING! smaller tsc: %" PRIu64 \
                       " -> %" PRIu64 " (%" PRIu64 " diff)\n",
                       tsc, new_tsc, tsc - new_tsc);
            } else if (new_tsc > tsc) {
                tsc = new_tsc;
                flush(tsc);
                line_.tsc_ = new_tsc;
            }
        } else if (type == ">") { // > for schedule in
            if (have_pending_output_line_)
            {
                queue_line_for_output();
            }
            queue_.switch_in(tsc);
            rest >> line_.cpu_;
        } else if (type == "<") { // > for schedule out
            if (have_pending_output_line_)
            {
                queue_line_for_output();
            }
            queue_.switch_out(tsc);
        } else if (type == "d") {
            if (have_pending_output_line_) {
                queue_line_for_output();
            }
            line_.type_ = type[0];
            rest >> line_.call_stack_level_;
            getline(rest, line_.rest_);
            queue_line_for_output();
        }
    }

    void flush(uint64_t tsc = 0)
    {
        if (have_pending_output_line_) {
            queue_line_for_output();
        }
        queue_.flush(tsc);
    }

    int                      low_water_mark_;
    bool                     have_pending_output_line_;
    output_line              line_;
    normalizing_output_queue queue_;
}; // per_cpu_data

bool xxx(tid_t tid, int low_water_mark, istream& is)
{
    bool ok = true;

    // single thread of execution; model as one cpu
    per_cpu_data  cpus(tid, low_water_mark);

    string        type;
    istringstream rest;

    while (get_intermediate_line(is, type, rest)) {
        cpus.parse(type, rest);
    }

    // Do not flush last lines with identical timestamps
    //cpus.flush();

    return ok;
}

}

using namespace sat;

void usage(const char* name)
{
    printf("Usage: %s [-c] [-d] [-m <System.map>] [-t <target-filesystem-root>] [-w <low-water-marks-path>]\n", name);
}

int main(int argc, char* argv[])
{
    string output_path;
    string system_map_path;
    string stack_low_water_marks_path;
    tid_t  tid;
    int    low_water_mark;

    int c;

    while ((c = getopt(argc, argv, ":cdo:w:")) != EOF) {
        switch (c) {
            case 'c':
                cap = true;
                break;
            case 'd':
                debug = true;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 'w':
                stack_low_water_marks_path = optarg;
                break;
            case '?':
                fprintf(stderr, "unknown option '%c'\n", optopt);
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
            case ':':
                fprintf(stderr, "missing filename to '%c'\n", optopt);
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (stack_low_water_marks_path == "") {
        fprintf(stderr, "must specify stack low-water mark file with -w\n");
        exit(EXIT_FAILURE);
    } else {
        FILE* f = fopen(stack_low_water_marks_path.c_str(), "r");
        if (!f) {
            fprintf(stderr,
                    "error opening stack low-water mark file for reading: %s\n",
                    stack_low_water_marks_path.c_str());
            exit(EXIT_FAILURE);
        }
        if (fscanf(f, "%u|%d\n", &tid, &low_water_mark) != 2) {
            fprintf(stderr,
                    "error reading low-water mark file: %s\n",
                    stack_low_water_marks_path.c_str());
            exit(EXIT_FAILURE);
        }
        fix_stacks = true;
    }

    ifstream input_file;
    if (optind < argc) {
        input_file.open(argv[optind]);
        if (!input_file) {
            fprintf(stderr,
                    "error opening input file for reading: %s\n",
                    argv[optind]);
            exit(EXIT_FAILURE);
        }
    }

    if (output_path != "") {
        output_file = fopen(output_path.c_str(), "w");
        if (!output_file) {
            fprintf(stderr,
                    "error opening output file for writing: %s\n",
                    output_path.c_str());
            exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        xxx(tid, low_water_mark, input_file);
    } else {
        xxx(tid, low_water_mark, cin);
    }

}
