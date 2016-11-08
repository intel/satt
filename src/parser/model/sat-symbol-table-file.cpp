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
#include "sat-symbol-table-file.h"
#include <cstdio>
#include <unistd.h>
#include <sys/file.h>

namespace sat {

using namespace std;

namespace {

void append_to_symbol_table_file(const string& symbol, unsigned id, FILE* file)
{
    fprintf(file, "%u;%s\n", id, symbol.c_str());
}

void read_new_entries(FILE* file, map<string, unsigned>& table)
{
    clearerr(file);

    unsigned id;
    char*    symbol = 0;
    while (fscanf(file, "%u;%ms\n", &id, &symbol) == 2) {
        table.insert({symbol, id});
        free(symbol);
        symbol = 0;
    }
}

} // anonymous namespace


symbol_table_file::symbol_table_file() :
    file_()
{
}

bool symbol_table_file::set_path(const string& path)
{
    bool ok = true;
    file_ = fopen(path.c_str(), "a+");
    if (file_ == nullptr) {
        fprintf(stderr, "error opening symbol table file '%s'\n for appending\n",
                path.c_str());
        ok = false;
    }

    return ok;
}

symbol_table_file::~symbol_table_file()
{
    fclose(file_);
}

bool symbol_table_file::insert(const std::string& symbol, unsigned& id)
{
    int  fd;
    bool is_new = true;

    id = 0;

    if ((fd = fileno(file_)) != -1) {
        if (flock(fd, LOCK_EX) != -1) {

            // read new entries from the file into the symbol table
            read_new_entries(file_, table_);

            // see if the symbol already exists
            auto i = table_.find(symbol);
            if (i != table_.end()) {
                // got the symbol from the file; use it
                id     = i->second;
                is_new = false;
            } else {
                // the symbol was not in the file; add it
                id     = table_.size() + 1;
                is_new = true;
                table_.insert({symbol, id});
                append_to_symbol_table_file(symbol, id, file_);
            }

            fflush(file_);

            flock(fd, LOCK_UN);
        }
    }

    return is_new;
}

} // namespace sat
