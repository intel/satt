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
#ifndef SAT_LOG_H
#define SAT_LOG_H

#include <cstdio>

#define SAT_LOG(level, format, ...) \
    if (global_debug_level >= level) std::printf("# " format, ##__VA_ARGS__);

#define SAT_PIPED_OUTPUT_PREFIX "@ "
#define SAT_OUTPUT_PREFIX       "! "

#define SAT_OUTPUT(kind, format, ...)   \
    std::printf(SAT_PIPED_OUTPUT_PREFIX \
                SAT_OUTPUT_PREFIX       \
                kind                    \
                format, ##__VA_ARGS__); \
    if (global_use_stderr)              \
        std::fprintf(stderr, format, ##__VA_ARGS__);


#define SAT_INFO(format, ...) SAT_OUTPUT("i", format, ##__VA_ARGS__)
#define SAT_WARN(format, ...) SAT_OUTPUT("w", format, ##__VA_ARGS__)
#define SAT_ERR(format, ...)  SAT_OUTPUT("e", format, ##__VA_ARGS__)

namespace sat {
    extern unsigned global_debug_level;
    extern bool     global_use_stderr;
}

#endif
