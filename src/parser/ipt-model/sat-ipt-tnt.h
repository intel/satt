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
#ifndef SAT_IPT_TNT_H
#define SAT_IPT_TNT_H

#include <queue>
#include <cinttypes>

namespace sat {

    using namespace std;

    class tnt_buffer {
    public:
        tnt_buffer() : current_(), buffered_() {}

        void append(uint64_t bits, uint64_t mask)
        {
            if (empty()) {
                current_.bits = bits;
                current_.mask = mask;
            } else {
                buffered_.push({bits, mask});
            }
        }

        bool empty()
        {
            return !current_.mask;
        }

        bool taken()
        {
            bool result = current_.bits & current_.mask;

            if (!(current_.mask >>= 1)) {
                if (!buffered_.empty()) {
                    current_ = buffered_.front();
                    buffered_.pop();
                }
            }

            return result;
        }

        unsigned size()
        {
            unsigned result = 0;

            tnt c        = current_;
            queue<tnt> b = buffered_;
            while (c.mask) {
                ++result;
                if (!(c.mask >>= 1) && !b.empty()) {
                    c = b.front();
                    b.pop();
                }
            }

            return result;
        }

        void clean()
        {
            buffered_ = queue<tnt>();
            current_.bits = 0;
            current_.mask = 0;
        }
    private:
        using tnt = struct {
            uint64_t bits;
            uint64_t mask;
        };
        tnt        current_;
        queue<tnt> buffered_;
    }; // tnt_buffer

} // sat

#endif // SAT_IPT_TNT_H
