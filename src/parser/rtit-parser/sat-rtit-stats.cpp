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
#include "sat-rtit-parser.h"
#include "sat-rtit-workarounds.h"
#include <cstdio>
#include <cstdlib>
#include <cinttypes>

using namespace sat;
using namespace std;

class rtit_stdin : public rtit_parser_input {
public:
    bool get_next(unsigned char& byte)
    {
        int c = fgetc(stdin);
        if (c != EOF) {
            byte = c;
            ++size;
            return true;
        } else {
            return false;
        }
    }

    bool bad() { return ferror(stdin); }

    void report() {
        printf("INPUT: %" PRIu64 " bytes\n", size);
    }

private:
    uint64_t size;
};

class rtit_stats : public rtit_parser_output {
public:
    void fup_buffer_overflow() { ++overflows; }

    void report() {
        printf("OVERFLOWS: %" PRIu64 "\n", overflows);
    }

private:
    uint64_t overflows;
};

typedef rtit_parser<postpone_early_mtc<
                   skip_after_overflow_with_compressed_lip>>
        parser_type;

int main(int argch, char* argv[])
{
    shared_ptr<rtit_stdin> input{new rtit_stdin};
    shared_ptr<rtit_stats> output{new rtit_stats};

    parser_type parser(input, output, output);

    if (parser.parse(false, true)) {
        input->report();
        output->report();
        printf("OK\n");
        exit(EXIT_SUCCESS);
    } else {
        input->report();
        output->report();
        printf("ERROR\n");
        exit(EXIT_FAILURE);
    }
}
