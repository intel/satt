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
#include <cstdio>
#include <cstdint>
#include <cinttypes>

using namespace std;

int main()
{
    uint64_t tsc, out_of_thread, in_thread, instruction_count;
    int32_t  stack_level;
    uint32_t cpu, thread, module, symbol;
    char     type;

    uint64_t lines = 0;

    while (fscanf(stdin,
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
        fprintf(stdout,
                "%" PRIu64 "|%d|%" PRIu64 "|%" PRIu64 "|%" PRIu64 "|%c|%u|%u|%u|%u\n",
                tsc,
                stack_level,
                out_of_thread,
                in_thread,
                instruction_count,
                type,
                cpu,
                thread,
                module,
                symbol);
        ++lines;
    }

    fprintf(stderr, "processed %" PRIu64 " lines\n", lines);
}
