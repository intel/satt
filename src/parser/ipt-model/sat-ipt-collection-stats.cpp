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
#include "sat-sideband-model.h"
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

    sideband_model sideband;
    if (!sideband.build(collection.sideband_path())) {
        fprintf(stderr, "sideband model building failed\n");
        exit(EXIT_FAILURE);
    }

    auto earliest_tsc = collection.earliest_tsc();
    auto tsc_tick     = sideband.tsc_tick();
    auto fsb_mhz      = sideband.fsb_mhz();

    cout << "first_tsc|" << earliest_tsc << "|TSC initial offset"      << endl
         << "TSC_TICK|"  << tsc_tick     << "|Time Stamp Counter tick" << endl
         << "FSB_MHZ|"   << fsb_mhz      << "|Front-side Bus MHz"      << endl;
}
