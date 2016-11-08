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
#include "sat-helper-path-mapper.h"
#include "sat-memory.h"
#include <cstdio>
#include <climits>
#include <cstring>

namespace sat {

namespace {
const uint32_t OUTPUT_MAX = PATH_MAX * 2;

bool run_helper(const string& helper_command_format_string,
                const string& target_path,
                string&       host_path,
                string&       symbols_path)
{
    bool success = false;

    char* helper_command;
    if (asprintf(&helper_command,
                 helper_command_format_string.c_str(),
                 target_path.c_str()) == -1)
    {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }

    // TODO: instead of popen() we should really use fork() and execvp()
    //       to be able to pass args without worrying about quoting
    FILE* p = popen(helper_command, "r");
    free(helper_command);
    if (p) {
        char pipe_output[OUTPUT_MAX + 1];
        if (fgets(pipe_output, OUTPUT_MAX + 1, p) != 0) {
            if (pipe_output[0] != '\0' &&
                pipe_output[strlen(pipe_output) - 1] == '\n')
            {
                pipe_output[strlen(pipe_output) - 1] = '\0';
            }
            if (pipe_output[0] != '\0') {
                int symbol_idx = 0;
                for (uint32_t i=0; i<strlen(pipe_output); ++i) {
                    if (pipe_output[i] == ';') {
                        pipe_output[i] = '\0';
                        host_path = pipe_output;
                        symbol_idx = i + 1;
                    }
                }
                if (symbol_idx > 0) {
                    symbols_path = &pipe_output[symbol_idx];
                } else {
                    host_path = pipe_output;
                    symbols_path = pipe_output;
                }
                success = true;
            }
        }
        (void)pclose(p);
    }

    return success;
}

} // anonymous namespace

struct helper_path_mapper::imp {
    string helper_command_format_string_;
}; // helper_path_mapper::imp

helper_path_mapper::helper_path_mapper(
    const string& cache_dir_path,
    const string& helper_command_format_string)
    : caching_path_mapper(cache_dir_path),
      imp_(new imp{helper_command_format_string})
{
}

helper_path_mapper::~helper_path_mapper()
{
}

bool helper_path_mapper::get_host_path(const string& target_path,
                                       string&       host_path,
                                       string&       symbols_path) const
{
    return run_helper(imp_->helper_command_format_string_,
                      target_path,
                      host_path,
                      symbols_path);
}

} // sat
