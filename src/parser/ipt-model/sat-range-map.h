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
#ifndef SAT_RANGE_MAP
#define SAT_RANGE_MAP

#include <utility>
#include <map>
#include <functional>
#include <iostream>

namespace sat {

using namespace std;

template <class LIMIT, class ID>
class range_map {
public:
    range_map() : ranges_()
    {}

    void insert(pair<LIMIT, LIMIT> range, ID& id)
    {
        insert_hole(range);
        ranges_.insert({range, id});
    }

    void insert_hole(pair<LIMIT, LIMIT> hole)
    {
        for (auto r = ranges_.begin(); r != ranges_.end(); ++r) {
            if (hole.first < r->first.second && hole.second > r->first.first) {
                // the hole intersects with the range
                if (r->first.first < hole.first) {
                    // need to make a new entry for beginning of r
                    ranges_.insert({{r->first.first, hole.first}, r->second});
                }
                if (r->first.second > hole.second) {
                    // need to make a new entry for end of r
                    ranges_.insert({{hole.second, r->first.second}, r->second});
                }
                ranges_.erase(r);
            }
        }
    }

    bool find(const LIMIT at, ID& id)
    {
        auto r = ranges_.lower_bound({at, at});
        if (r != ranges_.end() && r->first.first == at) {
                id = r->second;
                return true;
        }
        if (r != ranges_.begin()) {
            --r;
            if (r->first.first <= at && r->first.second > at) {
                id = r->second;
                return true;
            }
        }

        return false;
    }

    void iterate(function<void(const pair<LIMIT, LIMIT>&, const ID&)> f) const
    {
        for (const auto& r : ranges_) {
            f(r.first, r.second);
        }
    }

private:
    map<pair<LIMIT, LIMIT>, ID> ranges_;
}; // range_map

} // namespace sat

#endif // SAT_RANGE_MAP
