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
#include "sat-local-path-mapper.h"
#include <iostream>
#include <vector>

using namespace sat;

namespace {

void usage(const char* name)
{
    cout << "Usage: " << name
         << " <needle> [-k kernel] [-m modules-haystack]* [haystack]*" << endl;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        cerr << "too few arguments" << endl;
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    string         kernel_path;
    vector<string> modules_haystacks;
    vector<string> haystacks;

    for (int i = 2; i < argc; ++i) {
        if (string(argv[i]) == "-k") {
            if (++i < argc) {
                kernel_path = argv[i];
            } else {
                cerr << "must provide kernel path after -k" << endl;
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else if (string(argv[i]) == "-m") {
            if (++i < argc) {
                modules_haystacks.push_back(argv[i]);
            } else {
                cerr << "must provide module directory path after -m" << endl;
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else {
            haystacks.push_back(argv[i]);
        }
    }

    string needle = argv[1];
    bool   found  = false;
    string result;
    string syms = "";

    if (needle == "vmlinux" && kernel_path != "") {
        result = kernel_path;
        found  = true;
    } else if (needle.find(".ko") != string::npos && modules_haystacks.size()) {
        local_path_mapper mapper(modules_haystacks);
        found = mapper.find_kernel_module(needle, result);
    }

    if (result == "") {
        local_path_mapper mapper(haystacks);
        found = mapper.find_file(needle, result, syms);
    }

    if (found) {
        if (syms == "") {
            cout << result << endl;
        } else {
            cout << result << ";" << syms << endl;
        }
    }
}
