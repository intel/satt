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
#include "sat-every.h"
#include <signal.h>
#include <unistd.h>

namespace sat {

enum alarm_state {
    ALARM_NOT_SET, ALARM_SET, ALARM_EXPIRED
};

namespace {

alarm_state state = ALARM_NOT_SET;

void alarm_handler(int sig)
{
    state = ALARM_EXPIRED;
}

void set_alarm(unsigned seconds)
{
    struct sigaction sa;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = alarm_handler;
    sigaction(SIGALRM, &sa, 0);
    alarm(seconds);
}

}

void every_x_seconds(unsigned x, function<void(void)>& callback)
{
    switch (state) {
    case ALARM_EXPIRED:
        callback();
        // fall through
    case ALARM_NOT_SET:
        state = ALARM_SET;
        set_alarm(x);
        break;
    case ALARM_SET:
        // do nothing
        break;
    };
}

}
