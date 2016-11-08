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
#include "sat-system-map.h"
#include "sat-file-input.h"
#include <cstring>

namespace sat {

    bool system_map::read(const string& path)
    {
        bool done = false;

        line_based_file f;

        if (f.open(path)) {
            char*  line = 0;
            size_t n    = 0;

            while (f.get_line(&line, &n)) {
                rva   address;
                char  type;
                char* name = 0;
                if (sscanf(line, "%llx %c %ms", &address, &type, &name) == 3)
                {
                    if (type == 't' || type == 'T') {
                        functions_.insert({address, name});
                    }
                    if (strcmp(name, "_text") == 0) {
                        begin_ = address;
                    } else if (strcmp(name, "_end") == 0) {
                        end_ = address;
                    }
                    free(name);
                }
            }

            free(line);
        }

        return done;
    }

    bool system_map::get_function(rva       address,
                                  string&   function,
                                  unsigned& offset) const
    {
        bool got_it = false;

        if (begin_ <= address && address < end_) {
            auto i = functions_.upper_bound(address);
            if (i != functions_.begin()) {
                function = (--i)->second;
                if (i->first == address) {
                    offset = 0;
                    //function += " #"; // indicate the beginning of function
                } else {
                    offset = address - i->first;
                }
                got_it = true;
            }
        }

        return got_it;
    }

    bool system_map::get_address(const string& function, rva& address) const
    {
        bool got_it = false;

        for (auto i = functions_.begin(); i != functions_.end(); ++i) {
            if (i->second == function) {
                address = i->first;
                got_it = true;
                break;
            }
        }

        return got_it;
    }

}
