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
#ifndef SAT_TID_H
#define SAT_TID_H

#include <unistd.h>
#include <functional>

// tid_t - a type for uniquely identifying a thread
//
// All threads can be identified by pid_t, except thread 0.
// The problem with thread 0 is that each CPU has its own.
// By introducing a separate type that uniquely identifies
// all threads -- including thread 0 -- we don't have to make
// special cases for thread 0 in multiple places in the code.

namespace sat {

typedef unsigned tid_t;

void tid_add(pid_t pid, pid_t thread_id, unsigned cpu);
bool tid_get(pid_t thread_id, unsigned cpu, tid_t& tid);
bool tid_get_pid(pid_t thread_id, unsigned cpu, pid_t& pid);
bool tid_get_pid(tid_t tid, pid_t& pid);
bool tid_get_thread_id(tid_t tid, pid_t& thread_id);
bool tid_get_info(tid_t tid, pid_t& pid, pid_t& thread_id, unsigned& cpu);
pid_t tid_get_first_free_pid();

//                                  tid,   pid,   thread_id, cpu
void tid_iterate(std::function<bool(tid_t, pid_t, pid_t,     unsigned)> callback);

}

#endif
