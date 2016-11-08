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
#include "sat-scheduling-heuristics.h"
#include <cstdio>

using namespace sat;

int main(int argc, char* argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <cpu#> <rtit-file> <sideband-file>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    unsigned cpu           = atoi(argv[1]);
    string   rtit_path     = argv[2];
    string   sideband_path = argv[3];

    shared_ptr<sideband_model> sideband{new sideband_model};
    if (!sideband->build(sideband_path)) {
        exit(EXIT_FAILURE);
    }

    vm_sec_list_type dummy;
    scheduling_heuristics heuristics(cpu, rtit_path, sideband, dummy );
    heuristics.apply();
    heuristics.dump();
}
