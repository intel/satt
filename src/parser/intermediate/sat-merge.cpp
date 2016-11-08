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
#include <cstdlib>
#include <cstdint>
#include <cinttypes>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <memory>

using namespace std;
using namespace sat;

namespace {

struct input {
    bool getline()
    {
        bool got_it = false;
        while (std::getline(*file, line)) {
            if (sscanf(line.c_str(), "%" PRIu64, &tsc) == 1) {
                got_it = true;
                break;
            }
        }
        return got_it;
    }

    bool putline(ostream& to)
    {
#if 1
        static uint64_t prev_tsc = 0;
        if (prev_tsc > tsc) {
            cerr << "JUMPING BACK IN TIME: "
                 << prev_tsc << " -> " << tsc << endl;
        }
        prev_tsc = tsc;
#endif
        return static_cast<bool>(to << line << '\n');
    }

    shared_ptr<ifstream> file;
    uint64_t             tsc;
    string               line;
};



bool merge(vector<input>& from, ostream& to)
{
    vector<input>::iterator next;
    vector<input>::iterator first;

    while (from.size()) {
        first = from.end();
        next  = from.end();

        for (auto f = from.begin(); f != from.end(); ++f) {
            if (first == from.end() || f->tsc < first->tsc) {
                next = first;
                first = f;
            } else if (next == from.end() || f->tsc < next->tsc) {
                next = f;
            }
        }

        do {
            if (!first->putline(to) || !first->getline()) {
                from.erase(first);
                break;
            }
        } while (next == from.end() || first->tsc <= next->tsc);
    }

    return true;
}

void usage(const char* name)
{
    cout << "Usage: " << name << " <sat-output-files-to-merge>" << endl;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    vector<input> from;
    for (int i = 1; i < argc; ++i) {
        auto file = make_shared<ifstream>(argv[i]);
        if (!*file) {
            cerr << "could not open " << argv[i] << " for reading" << endl;
            exit(EXIT_FAILURE);
        } else {
            input i;

            i.file = file;
            if (i.getline()) {
                from.push_back(i);
            }
        }
    }

    if (!merge(from, cout)) {
        cerr << "error merging" << endl;
        exit(EXIT_FAILURE);
    }
}
