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
#ifndef SAT_SYMBOL_TABLE_FILE
#define SAT_SYMBOL_TABLE_FILE

#include <string>
#include <map>

namespace sat {

using namespace std;

class symbol_table_file
{
public:
    symbol_table_file();
    ~symbol_table_file();

    bool set_path(const string& path);
    bool insert(const string& symbol, unsigned& id);

protected:
    map<string, unsigned> table_;

private:
    symbol_table_file(const symbol_table_file&) = delete;

    FILE* file_;
}; // symbol_table_file

class file_backed_symbol_table : protected symbol_table_file
{
public:
    file_backed_symbol_table()
    {}

    bool set_path(const string& path)
    {
        return symbol_table_file::set_path(path);
    }

    bool get_new_id(const string& symbol, unsigned& id)
    {
        bool is_new;

        auto i = table_.find(symbol);
        if (i != table_.end()) {
            // symbol found
            is_new = false;
            id = i->second;
        } else {
            // symbol not found; add it
            is_new = insert(symbol, id);
        }

        return is_new;
    }
}; // file_backed_symbol_table

} // namespace sat

#endif // SAT_SYMBOL_TABLE_FILE
