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
#include "sat-ipt-collection.h"
#include "sat-ipt-iterator.h"
#include <fstream>
#include <cinttypes>

using namespace std;
using namespace sat;

void usage(const char* name)
{
    printf("Usage: %s <collection-file>\n", name);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    ifstream input(argv[1]);
    ipt_collection collection(input);

    uint64_t    earliest_tsc = collection.earliest_tsc();
    uint64_t    latest_tsc = earliest_tsc;
    bool        output_cbr = false;
    
    unsigned    cpu = 0;

    /* new implementation */
    for (const auto& p : collection.ipt_paths()) {
        ipt_iterator ipt(p);
        ipt.iterate([&](const ipt_iterator::token& t, size_t offset) {
            if (t.func == &ipt_iterator::output::tsc) {
                if (t.tsc >= earliest_tsc) {
                    output_cbr = true;
                    latest_tsc = t.tsc;
                } else {
                    output_cbr = false;
                }
            }
            if (t.func == &ipt_iterator::output::cbr) {
                if (output_cbr) {
                    printf("%" PRIu64 "|%u|%u|%u\n", 
                           latest_tsc - earliest_tsc,
                           cpu,
                           t.cbr,
                           t.cbr);
                }
            }
        });
        ++cpu;
    }
}
