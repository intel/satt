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
#include "sat-filesystem.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

using namespace std;

string basename(const string& path)
{
    string result;

    string::size_type first;
    string::size_type last;

    last = path.find_last_not_of('/');
    if (last != string::npos) {
        first = path.rfind('/', last);
        if (first == string::npos) {
            first = 0;
        } else {
            ++first;
        }

        result = path.substr(first, last - first + 1);
    }

    return result;
}

bool do_find_path(const string&                       needle,
                  const string&                       haystack,
                  function<void(const string& match)> callback)
{
    bool found = false;

    sat::recurse_dir(haystack, [&](const string& dir, const string& file) {
        if (file == needle) {
            found = true;
            callback(dir + "/" + file);
        }
        return true;
    });

    return found;
}

} // anonymous namespace

namespace sat {

bool find_path(const string&                       needle,
               const string&                       haystack,
               function<void(const string& match)> callback)
{
    return do_find_path(basename(needle), haystack, callback);
}

bool is_dir(const string& path)
{
    bool is = false;

    if (DIR* d = opendir(path.c_str())) {
        is = true;
        closedir(d);
    }

    return is;
}

bool recurse_dir(const string&                                         path,
                 function<bool(const string& dir, const string& file)> callback)
{
    bool done = false;

    if (DIR* d = opendir(path.c_str())) {
        while (struct dirent* de = readdir(d)) {
            string name = de->d_name;
            if (!callback(path, name)) {
                break;
            }
            string subpath = path + "/" + name;
            struct stat sb;
            if (stat(subpath.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
                if (name != "." && name != "..") {
                    recurse_dir(subpath, callback);
                }
            }
        }
        closedir(d);
        done = true;
    }

    return done;
}

unsigned file_size(const string& path)
{
    unsigned size = 0;

    int fd = open(path.c_str(), O_RDONLY);
    if (fd != -1) {
        struct stat sb;
        if (fstat(fd, &sb) == 0) {
            size = sb.st_size;
        }
        close(fd);
    }

    return size;
}

} // namespace sat
