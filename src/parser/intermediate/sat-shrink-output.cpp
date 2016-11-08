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
#include <unistd.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstdint>
#include <cinttypes>
#include <string>
#include <map>
#include <fstream>
#include <iostream>

using namespace std;
using namespace sat;

namespace {

typedef map<tid_t, int> stack_low_water_marks;

stack_low_water_marks low_water_marks;


bool shrink(istream& from, ostream& to, size_t& new_size)
{
    tid_t current_thread         = 0;
    int   current_low_water_mark;

    current_low_water_mark = 0;
    auto m = low_water_marks.find(current_thread);
    if (m != low_water_marks.end()) {
        current_low_water_mark = m->second;
    }

    uint64_t tsc, out_of_thread, in_thread, instruction_count;
    int32_t  stack_level;
    uint32_t cpu, thread, module, symbol;
    char     type;

    string line;
    while (getline(from, line)) {
        if (sscanf(line.c_str(),
                   "%" PRIu64 "|%d|%" PRIu64 "|%" PRIu64 "|%" PRIu64 "|%c|%u|%u|%u|%u\n",
                   &tsc,
                   &stack_level,
                   &out_of_thread,
                   &in_thread,
                   &instruction_count,
                   &type,
                   &cpu,
                   &thread,
                   &module,
                   &symbol) == 10)
        {
            // it's normal output line; shrink-wrap it

            if (thread != current_thread) {
                // context has switched, get a new stack low-water mark
                current_thread = thread;
                current_low_water_mark = 0;
                auto m = low_water_marks.find(current_thread);
                if (m != low_water_marks.end()) {
                    current_low_water_mark = m->second;
                }
            }
            // output the shrink-wrapped line
            to <<        tsc
               << '|' << stack_level - current_low_water_mark
               << '|' << out_of_thread
               << '|' << in_thread
               << '|' << instruction_count
               << '|' << type
               << '|' << cpu
               << '|' << thread
               << '|' << module
               << '|' << symbol
               << '\n';
            if (!to) {
                cerr << "error writing output" << endl;
                return false;
            }
        } else {
            // output debugging lines as is
            to << line << '\n';
        }
    }

    if (from.bad()) {
        cerr << "error reading input" << endl;
        return false;
    }

    new_size = to.tellp();

    return true;
}

void usage(const char* name)
{
    cout << "Usage: "
         << name
         << " <sat-output-file-to-shrink> [-w low-water-marks-file]"
         << endl;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 4) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* path = 0;
    const char* low_water_marks_path = 0;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "-w") {
            if (++i < argc) {
                low_water_marks_path = argv[i];
            } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else if (argv[i][0] != '-') {
            path = argv[i];
        } else {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (path == 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    ifstream from(path);
    if (!from) {
        cerr << "could not open " << path << " for reading" << endl;
        exit(EXIT_FAILURE);
    }

    ofstream to(path, ios::in | ios::out);
    if (!to) {
        cerr << "could not open " << path << " for writing" << endl;
        exit(EXIT_FAILURE);
    }

    if (low_water_marks_path) {
        ifstream lwm(low_water_marks_path);
        if (!lwm) {
            cerr << "could not open "
                 << low_water_marks_path
                 << " for reading" << endl;
            exit(EXIT_FAILURE);
        }
        tid_t    tid;
        int      low_water_mark;
        unsigned count = 0;
        char     c;
        while (lwm >> tid >> c >> low_water_mark) {
            low_water_marks.insert({tid, low_water_mark});
            ++count;
        }
    }


    size_t new_size;
    if (!shrink(from, to, new_size)) {
        cerr << "error shrinking " << path << endl;
        exit(EXIT_FAILURE);
    }


    to.close();
    if (to.bad()) {
        cerr << "problem closing " << path << endl;
        exit(EXIT_FAILURE);
    }

    from.close();
    if (from.bad()) {
        cerr << "problem closing " << path << endl;
        exit(EXIT_FAILURE);
    }

    if (truncate(path, new_size) == -1) {
        cerr << "problem truncating " << path << " to shrinked size" << endl;
        exit(EXIT_FAILURE);
    }
}
