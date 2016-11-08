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
#include "sat-ipt-scheduling-heuristics.h"
#include "sat-ipt-tsc-heuristics.h"

using namespace sat;

int main(int argc, char* argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s [cpu_id] [ipt-file] [sideband_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    unsigned cpu           = atoi(argv[1]);
    string   ipt_path      = argv[2];
    string   sideband_path = argv[3];

    shared_ptr<sideband_model> sideband{new sideband_model};
    if (!sideband->build(sideband_path)) {
        exit(EXIT_FAILURE);
    }

    auto schedulings = make_shared<scheduling_heuristics>(cpu, ipt_path, sideband, sideband_path);
    schedulings->apply();
    schedulings->dump();
}
