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
#include "sat-demangle.h"
#include <cxxabi.h>

using namespace std;

namespace sat {

#define PLT "@plt"
#define STR_LIT_LEN(s) (sizeof(s) / sizeof(s[0]) - 1)

// TODO: make these non-const and obtain from command line
const bool              cap      = true;
const string::size_type cap_size = 256;

string demangle(const string& name)
{
    string result;
    bool   plt = false;

    string::size_type at = name.rfind(PLT[0]);
    if (at != string::npos &&
        at + STR_LIT_LEN(PLT) == name.size() &&
        name.compare(at, STR_LIT_LEN(PLT), PLT) == 0)
    {
        // name ends in "@plt"
        plt = true;
    }
    string pltless = plt ? name.substr(0, at) : name;
    size_t length;
    int    status;

    using namespace __cxxabiv1; // TODO
    char* demangled = __cxa_demangle(pltless.c_str(), 0, &length, &status);
    if (demangled) {
        result = demangled;
        free(demangled);
    } else {
        result = pltless;
    }

    if (cap) {
        const string::size_type new_size = plt ? cap_size - 4 : cap_size;
        if (result.size() > new_size) {
            // TODO: use a more clever way to shorten the name,
            // e.g. jlaitin's idea of shortening and adding a hash
            result.resize(new_size);
        }
    }

    if (plt) {
        result += PLT;
    }

    return result;
}

} // namespace sat
