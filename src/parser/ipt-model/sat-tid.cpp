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
#include "sat-tid.h"
#include "sat-log.h"
#include <sstream>
#include <map>
#include <algorithm>

namespace sat {

using namespace std;

typedef pair<pid_t /* thread id */, unsigned /* cpu */> unique_tid;

namespace {

bool adding = true;

typedef map<unique_tid, pid_t> pid_map;
pid_map pids;

typedef map<unique_tid, tid_t> tid_map;
tid_map tids;

unique_tid make_unique(pid_t thread_id, unsigned cpu)
{
    // only use the cpu # for thread 0
    if (thread_id != 0) {
        cpu = 0;
    }
    return make_pair(thread_id, cpu);
}

void assign_tids()
{
    SAT_LOG(1, "assigning tids\n");
    tid_t tid_count = 0;
    for (auto& p : pids) {
        SAT_LOG(1, "%d => %u\n", p.first.first, tid_count);
        tids[p.first] = tid_count++;
    }
}

}

void tid_add(pid_t pid, pid_t thread_id, unsigned cpu)
{
    if (adding) {
        pids[make_unique(thread_id, cpu)] = pid;
    } else {
        fprintf(stderr, "WARNING: ADDING A THREAD AFTER ASSIGNING IDS\n");
    }
}

bool tid_get(pid_t thread_id, unsigned cpu, tid_t& tid)
{
    bool found = false;

    if (adding) {
        assign_tids();
        adding = false;
    }
    auto t = tids.find(make_unique(thread_id, cpu));
    if (t != tids.end()) {
        tid = t->second;
        found = true;
    }

    return found;
}

bool tid_get_pid(pid_t thread_id, unsigned cpu, pid_t& pid)
{
    bool found;

    auto i = pids.find(make_unique(thread_id, cpu));
    if (i == pids.end()) {
        SAT_LOG(0, "COULD NOT FIND PID FOR THREAD_ID %d\n", thread_id);
        found = false;
    } else {
        pid = i->second;
        found = true;
        SAT_LOG(0, "MAPPED THREAD_ID %d -> PID %d\n", thread_id, pid);
    }

    return found;
}

static tid_map::const_iterator tid_find(tid_t tid)
{
    return find_if(tids.begin(), tids.end(), [tid](tid_map::const_reference t) {
                                                 return t.second == tid;
                                             });
}

bool tid_get_pid(tid_t tid, pid_t& pid)
{
    bool found = false;

    const auto& t = tid_find(tid);
    if (t != tids.end()) {
        pid =  pids[t->first];
        found = true;
        SAT_LOG(1, "MAPPED TID %u -> PID %d\n", tid, pid);
    } else {
        SAT_LOG(0, "COULD NOT FIND PID FOR TID %d\n", tid);
    }

    return found;
}

bool tid_get_thread_id(tid_t tid, pid_t& thread_id)
{
    bool found = false;

    const auto& t = tid_find(tid);
    if (t != tids.end()) {
        thread_id = t->first.first;
        found = true;
        SAT_LOG(0, "MAPPED TID %d -> THREAD_ID %d\n", tid, thread_id);
    } else {
        SAT_LOG(0, "COULD NOT FIND THREAD_ID FOR TID %d\n", tid);
    }

    return found;
}

bool tid_get_info(tid_t tid, pid_t& pid, pid_t& thread_id, unsigned& cpu)
{
    bool found = false;
    const auto& t = tid_find(tid);
    if (t != tids.end()) {
        pid = pids[t->first];
        thread_id = t->first.first;
        cpu = t->first.second;
        found = true;
    } else {
        SAT_LOG(0, "COULD NOT FIND THREAD_ID FOR TID %d\n", tid);
    }

    return found;
}

//                                  tid,   pid,   thread_id, cpu
void tid_iterate(std::function<bool(tid_t, pid_t, pid_t,     unsigned)> callback)
{
    for (auto& t : tids) {
        //            tid,      pid,           thread_id,     cpu
        if (!callback(t.second, pids[t.first], t.first.first, t.first.second)) {
            break;
        }
    }
}


pid_t tid_get_first_free_pid()
{
    pid_t i;
    bool found = false;

    for (i=1; i<0x7FFFFFF0; i++) {
        found = false;
        auto it = pids.begin();
        for (; it != pids.end(); it++) {
            //printf("tid_get_first_free_pid(): %d (%d)\n", it->second, i);
            if (it->second == i) {
                found = true;
                break;
            }
        }
        if (!found)
            return i;
    }
    return i;
}

} // namespace sat
