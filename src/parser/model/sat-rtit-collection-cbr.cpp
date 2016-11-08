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
#include "sat-rtit-collection.h"
#include "sat-rtit-iterator.h"
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
    rtit_collection collection(input);

    uint64_t earliest_tsc = collection.earliest_tsc();

    unsigned cpu = 0;
    for (const auto& p : collection.rtit_paths()) {
        rtit_iterator rtit(p);
        rtit.iterate([=](const rtit_parser_output::item& parsed) {
            if (parsed.token == &rtit_parser_output::sts) {
                if (parsed.sts.tsc >= earliest_tsc) {
                    printf("%" PRIu64 "|%u|%u|%u\n",
                           parsed.sts.tsc - earliest_tsc,
                           cpu,
                           parsed.sts.acbr,
                           parsed.sts.ecbr);
                }
            }
        });
        ++cpu;
    }
}
