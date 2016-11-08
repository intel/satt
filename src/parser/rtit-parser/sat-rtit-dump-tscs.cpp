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
#include "sat-rtit-tsc.h"
#include <cstdio>
#include <string>
#include <cinttypes>

using namespace sat;

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <rtit-trace-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    tsc_span_iterator iterator(argv[1], true, 3, 0);
    if (!iterator) {
        fprintf(stderr, "Could not open file '%s' for reading\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    rtit_offset offset;
    size_t      rtit_span;
    uint64_t    tsc;
    size_t      tsc_span;
    while (iterator.get_next(offset, rtit_span, tsc, tsc_span)) {
        printf("%08" PRIx64 ":%04" PRIx64 "]|%08" PRIx64 ":%08" PRIx64 "\n",
               offset, (uint64_t)rtit_span, tsc, (uint64_t)tsc_span);
    }
}
