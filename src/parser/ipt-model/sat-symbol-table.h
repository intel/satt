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
#ifndef SAT_SYMBOL_TABLE_H
#define SAT_SYMBOL_TABLE_H

#include <string>
#include <map>

namespace sat {

class symbol_table
{
    public:
        bool get_new_id(const std::string& symbol, unsigned& id)
        {
            bool is_new;

            auto i = table_.lower_bound(symbol);
            if (i != table_.end() && i->first == symbol) {
                is_new = false;
                id = i->second;
            } else {
                is_new = true;
                id = table_.size() + 1;
                table_.insert(i, {symbol, id});
            }

            return is_new;
        }

    private:
        std::map<std::string, unsigned> table_;
};

} // namespace sat

#endif
