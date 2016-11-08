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
#include "sat-ipt-tsc-heuristics.h"
#include "sat-ipt-parser-sideband-info.h"

using namespace sat;

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [ipt-file] [sideband-file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    tsc_heuristics heuristics(argv[2]);
    if (!heuristics.parse_ipt(argv[1])) {
        fprintf(stderr, "error parsing IPT file '%s'\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    heuristics.apply();
    heuristics.dump();
}
