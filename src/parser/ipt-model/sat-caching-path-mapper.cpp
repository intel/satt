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
#include "sat-caching-path-mapper.h"
#include "sat-md5.h"
#include "sat-getline.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <climits>

namespace sat {

void append_to_cache(const string& target_path,
                     const string& host_path,
                     const string& sym_path,
                     FILE*         file)
{
    fprintf(file,
            "%s%s%s\n",
            quote(target_path).c_str(),
            quote(host_path).c_str(),
            quote(sym_path).c_str());
    fflush(file);
}

void read_new_entries(FILE* file_cache, map<string, pair<string, string>>& ram_cache)
{
    clearerr(file_cache);

    char cache_line[4 * PATH_MAX + 5]; // enough for 2 quoted paths
    while (fgets(cache_line, sizeof(cache_line), file_cache) != 0) {
        istringstream is(cache_line);
        string target_path;
        string host_path;
        string sym_path;
        if (!dequote(is, target_path) || !dequote(is, host_path) || !dequote(is, sym_path)) {
            fprintf(stderr, "path cache is corrupted\n");
            exit(EXIT_FAILURE);
        }
        ram_cache.insert({target_path, {host_path, sym_path}});
    }
}

caching_path_mapper::caching_path_mapper(const string& cache_dir_path) :
    file_cache_(nullptr), cache_dir_path_(cache_dir_path)
{
    open();
}

caching_path_mapper::~caching_path_mapper()
{
    close();
}

void caching_path_mapper::open()
{
    if (!file_cache_) {
        struct stat sb;

        if (stat(cache_dir_path_.c_str(), &sb) != 0 &&
            mkdir(cache_dir_path_.c_str(), 0777) != 0)
        {
            fprintf(stderr,
                    "error making cache dir '%s' for path resolution\n",
                    cache_dir_path_.c_str());
            exit(EXIT_FAILURE);
        }
        string file_cache_path = cache_dir_path_ + "/cache";
        file_cache_ = fopen(file_cache_path.c_str(), "a+");
        if (file_cache_ == nullptr) {
            fprintf(stderr,
                    "error opening cache file '%s' for path resolution\n",
                    file_cache_path.c_str());
            exit(EXIT_FAILURE);
        }
    }
}

void caching_path_mapper::close()
{
    if (file_cache_) {
        fclose(file_cache_);
        file_cache_ = nullptr;
    }
}

bool caching_path_mapper::find_file(const string& target_path,
                                    string&       host_path,
                                    string&       sym_path) const
{
    bool found = false;

    auto i = ram_cache_.find(target_path);
    if (i != ram_cache_.end()) {
        if (i->second.first != "" && i->second.second != "") {
            // host path exists in cache
            host_path = i->second.first;
            sym_path = i->second.second;
            found = true;
        } else {
            // resolving the host path has failed previously; return not found
        }
    } else {
        // host path not in cache yet; resolve it now
        found = resolve_host_path(target_path, host_path, sym_path);
    }

    return found;
}

bool caching_path_mapper::resolve_host_path(const string& target_path,
                                            string&       host_path,
                                            string&       sym_path) const
{
    // if this function gets called, target path was not found in memory cache
    bool found = false;

    // first, obtain a lock on a per target path lock
    string lock_path = cache_dir_path_ + "/" + md5(target_path) + ".lock";
    int    lock_fd;
    errno = 0;
    if ((lock_fd = ::open(lock_path.c_str(), O_CREAT | O_EXCL, 0644)) == -1) {
        if (errno == EEXIST) {
            // someone else is already resolving the path
            if ((lock_fd = ::open(lock_path.c_str(), 0)) != -1) {
                // path resolution is still going on; wait for it to finish
                flock(lock_fd, LOCK_EX);
                // path resolution done; remove the lock
                flock(lock_fd, LOCK_UN);
                ::close(lock_fd);
                unlink(lock_path.c_str());
            }
            // read new entries from file to memory
            flock(fileno(file_cache_), LOCK_EX);
            read_new_entries(file_cache_, ram_cache_);
            flock(fileno(file_cache_), LOCK_UN);

            // pick up the resolved path
            auto i = ram_cache_.find(target_path);
            if (i != ram_cache_.end() && i->second.first != "" && i->second.second != "") {
                host_path = i->second.first;
                sym_path = i->second.second;
                found     = true;
            }
        } else {
            // something went wrong with creating the lock file
            fprintf(stderr, "error creating lock file '%s'\n",
                    lock_path.c_str());
            exit(EXIT_FAILURE);
        }
    } else {
        // created the lock file; now lock it
        flock(lock_fd, LOCK_EX);

        // check if the path is in the file cache
        // read new entries from file to memory
        flock(fileno(file_cache_), LOCK_EX);
        read_new_entries(file_cache_, ram_cache_);

        auto i = ram_cache_.find(target_path);
        if (i != ram_cache_.end()) {
            if (i->second.first != "" && i->second.second != "") {
                // host path exists in the cache
                host_path = i->second.first;
                sym_path = i->second.second;
                found     = true;
            }
        } else {
            // host path not resolved yet; let's do it now
            string new_path; // target_path and host_path can be the same string!
            string new_sym_path;
            if (get_host_path(target_path, new_path, new_sym_path)) {
                found = true;
            } else {
                // host path does not exist; mark it so in the cache
                new_path = "";
                new_sym_path = "";
            }
            // add the result in the caches
            ram_cache_.insert({target_path, {new_path, new_sym_path}});
            append_to_cache(target_path, new_path, new_sym_path, file_cache_);
            // assign the new path to the out-variable
            host_path = new_path;
            sym_path = new_sym_path;
        }
        flock(fileno(file_cache_), LOCK_UN);

        flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        unlink(lock_path.c_str());
    }

    return found;
}

} // sat
