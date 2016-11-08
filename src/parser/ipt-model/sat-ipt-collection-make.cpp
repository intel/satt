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

using namespace sat;

void usage(const char* name)
{
    printf("Usage: %s -s <sideband-file> -t <ipt-file> \n", name);
}

int main(int argc, char* argv[])
{
    string         sideband_path;
    vector<string> ipt_paths;
    int            c;

    while ((c = getopt(argc, argv, ":s:t:r:")) != EOF) {
        switch (c) {
        case 's':
            sideband_path = optarg;
            break;
        case 't':
            ipt_paths.push_back(optarg);
            break;
        // TODO this is for backward compatibility, maybe remove 't' option
        case 'r':
            ipt_paths.push_back(optarg);
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

    if (sideband_path.empty() || ipt_paths.empty()) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    ipt_collection collection(sideband_path, ipt_paths);
    collection.serialize(cout);
} // main
