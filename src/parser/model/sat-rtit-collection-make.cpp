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

using namespace sat;

void usage(const char* name)
{
    printf("Usage: %s [-s <sideband-file>]"
                    " [-r <rtit-files>]"
                    " [-V <vm-binary-file>]"
                    "\n", name);
}

int main(int argc, char* argv[])
{
    string sideband_path;
    string vm_x86_64_path;
    vector<string> rtit_paths;
    vector<string> vm_binary_paths;
    int c;

    while ((c = getopt(argc, argv, ":s:r:V:x:")) != EOF)
    {
        switch (c) {
            case 's':
                sideband_path = optarg;
                break;
            case 'r':
                rtit_paths.push_back(optarg);
                break;
            case 'V':   // VM binary
                vm_binary_paths.push_back(optarg);
                break;
            case 'x':   // VM x86_64 binaries
                vm_x86_64_path = optarg;
                break;
            case '?':
                fprintf(stderr, "unknown option '%c'\n", optopt);
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
            case ':':
                fprintf(stderr, "missing filename to '%c'\n", optopt);
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (sideband_path.empty() || rtit_paths.empty())
    {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    rtit_collection collection(sideband_path, rtit_paths, vm_binary_paths, vm_x86_64_path);
    collection.serialize(cout);
}
