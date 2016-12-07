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
#include "sat-filesystem.h"
//#include "sat-elf.h"
#include "sat-log.h"
#include <map>
#include <algorithm>
#include <sys/stat.h>

namespace sat {

namespace {

string normalized_kernel_module_name(const string& name)
{
    string result = name;

    for (auto& c : result) {
        c = c == '-' ? '_' : tolower(c);
    }

    return result;
}

int common_tail_length(const string& a, const string& b)
{
    auto diff = mismatch(a.rbegin(), a.rend(), b.rbegin());
    return diff.first - a.rbegin();
}

bool find_file(const vector<string>& haystacks,
               const string&         needle,
               string&               result)
{
    bool found = false;

    // collect all paths that have a file with the same name as needle
    vector<string> needles;
    for (const auto& h : haystacks) {
        find_path(needle, h, [&needles](const string& match) {
                                 needles.push_back(match);
                             });
    }

    // pick the path that has the longest equal tail with the needle
    string longest_match;
    int    longest_match_len = 0;
    for (const auto& n : needles) {
        int l = common_tail_length(needle, n);
        if (l > longest_match_len) {
            longest_match_len = l;
            longest_match     = n;
            // defer assignment to result (it might be that needle === result)
        }
    }
    if (longest_match != "") {
        result = longest_match;
        found  = true;
    }

    return found;
}

} // anonymous namespace

struct local_path_mapper::imp {
    vector<string>                          haystacks_;
    mutable map<string, pair<bool, string>> cache_;
}; // local_path_mapper::imp

local_path_mapper::local_path_mapper(const vector<string>& haystacks) :
    imp_(new imp{haystacks})
{
}

local_path_mapper::~local_path_mapper()
{
}

bool local_path_mapper::find_file(const string& file, string& result, string& syms) const
{
    bool found = false;

    const auto& p = imp_->cache_.find(file);
    if (p != imp_->cache_.end()) {
        found  = p->second.first;
        result = p->second.second;
        syms = result;
    } else {
        string needle(file); // must make a copy in case file === result
        found = sat::find_file(imp_->haystacks_, needle, result);
        if (!found) {
            static string not_found("[not found] " + needle);
            result = not_found;
            syms = not_found;
            //SAT_WARN("missing target filesystem file '%s'\n", needle.c_str());
        }
        imp_->cache_.insert({needle, {found, result}});
    }

    return found;
}

bool local_path_mapper::find_kernel_module(const string& module,
                                           string&       result) const
{
    bool found = false;

    string module_name = normalized_kernel_module_name(module);

    for (const auto& h : imp_->haystacks_) {
        recurse_dir(h, [&](const string& dir, const string& file) {
            if (normalized_kernel_module_name(file) == module_name) {
                result = dir + "/" + file;
                found  = true;
            }
            return !found; // keep looking if not found
        });
    }

    return found;
}

bool local_path_mapper::find_dir(const string& dir, string& result) const
{
    return sat::find_file(imp_->haystacks_, dir, result) && is_dir(result);
}

} // namespace sat
