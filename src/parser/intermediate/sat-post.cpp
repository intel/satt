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
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstdarg>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace std;
using namespace sat;

namespace {

const char ui_information_marker = '!';

unsigned total_issues        = 0;
unsigned total_lost          = 0;
unsigned total_overflows     = 0;
unsigned total_skipped       = 0;
unsigned max_stack_depth     = 0;
unsigned tsc_tick            = 0;
unsigned fsb_mhz             = 0;
uint64_t first_tsc = 0;

int console_width()
{
    int width = 80;

    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
    }

    return width;
}

void console_print(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);

    int max_width = console_width();
    char buf[max_width + 1];
    int width = vsnprintf(buf, sizeof(buf), format, ap);
    if (width < max_width) {
        sprintf(&buf[width], "%*s", max_width - width, "");
    }

    printf("\r%s", buf);

    va_end(ap);
}

void console_message(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);

    int min_width = console_width();
    printf("\r");
    int width = vprintf(format, ap);
    if (width < min_width) {
        printf("%*s", min_width - width, "");
    }
    printf("\n");

    va_end(ap);
}

void print_status()
{
    console_print("SKIP: %u | OVF: %u | LOST: %u | STACK: %u | ISSUES: %u",
           total_skipped,
           total_overflows,
           total_lost,
           max_stack_depth,
           total_issues);
}

function<void(void)> update_ui = print_status;

bool process_depth(istringstream& line)
{
    unsigned stack_depth;
    line >> stack_depth;
    if (stack_depth > max_stack_depth) {
        max_stack_depth = stack_depth;
    }

    return true;
}

bool process_error(istringstream& line, ostream& output)
{
    string s;
    getline(line, s);
    s = "ERROR: " + s;
    output << s << endl;
    ++total_issues;
    console_message("%s", s.c_str());
    update_ui();
    return true;
}

bool process_frequencies(istringstream& line)
{
    line >> tsc_tick >> fsb_mhz;
    return true;
}

bool process_info(istringstream& line, ostream& output)
{
    string s;
    getline(line, s);
    output << s << endl;
    ++total_issues;
    return true;
}

bool process_lost(istringstream& line)
{
    ++total_lost;
    return true;
}

bool process_overflow(istringstream& line)
{
    ++total_overflows;
    return true;
}

bool process_skip(istringstream& line)
{
    unsigned skipped;
    line >> skipped;
    total_skipped += skipped;
    return true;
}

bool process_warning(istringstream& line, ostream& output)
{
    string s;
    getline(line, s);
    s = "WARNING: " + s;
    output << s << endl;
    ++total_issues;
    console_message("%s", s.c_str());
    update_ui();
    return true;
}

bool process_first_tsc(istringstream& line)
{
    line >> first_tsc;
    return true;
}

bool process_information_line(istringstream& line, ostream& output)
{
    bool processed = false;

    char marker;
    line >> marker;

    switch (marker) {
        case 'd': // stack depth
            processed = process_depth(line);
            break;
        case 'e': // textual information (an error) for the user
            processed = process_error(line, output);
            break;
        case 'f': // frequency information
            processed = process_frequencies(line);
            break;
        case 'g': // first_tsc info for generic messages
            processed = process_first_tsc(line);
            break;
        case 'i': // textual information for the user
            processed = process_info(line, output);
            break;
        case 'l': // lost when reconstructing RTIT execution
            processed = process_lost(line);
            break;
        case 'o': // RTIT overflow count
            processed = process_overflow(line);
            break;
        case 's': // skipped RTIT
            processed = process_skip(line);
            break;
        case 'w': // textual information (a warning) for the user
            processed = process_warning(line, output);
            break;
        default:
            break;
    }

    return processed;
}

bool sift_ui_information_line(istream&       is,
                              istringstream& line,
                              ostream&       other)
{
    bool got_it = false;

    string full_line;
    while (getline(is, full_line)) {
        line.seekg(0);
        line.str(full_line);

        char marker;
        line >> marker;
        if (marker  == ui_information_marker) {
            got_it = true;
            break;
        } else {
            other << full_line << endl;
        }
    }

    return got_it;
}

void usage(const char* path)
{
    printf("Usage: %s -o <log-output-path> [-s <stats-output-path>\n", path);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    if (argc != 3 && argc != 5) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* log_path   = 0;
    const char* stats_path = 0;

    for (int i = 1; i < argc; i += 2) {
        if (string("-o") == argv[i]) {
            log_path = argv[i + 1];
        } else if (string("-s") == argv[i]) {
            stats_path = argv[i + 1];
        } else {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }


    if (!log_path) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    ofstream stats;
    if (stats_path) {
        stats.open(stats_path);
        if (!stats) {
            fprintf(stderr, "could not open '%s' for writing\n", stats_path);
            exit(EXIT_FAILURE);
        }
    }

    ofstream output(log_path);
    if (!output) {
        fprintf(stderr, "could not open '%s' for writing\n", argv[2]);
        exit(EXIT_FAILURE);
    }
    printf("any possible issues will be written to '%s'\n", argv[2]);

    istringstream line;
    while (sift_ui_information_line(cin, line, output)) {
        process_information_line(line, output);
        every_x_seconds(1, update_ui);
    }
    update_ui();
    printf("\n");

    if (stats_path) {
        stats << "Skipped|"
              << total_skipped
              << "|Total bytes of RTIT that were unusable and thus skipped"
              << endl;
        stats << "Overflows|"
              << total_overflows
              << "|Total number of RTIT overflow packets"
              << endl;
        stats << "Lost|"
              << total_lost
              << "|Total number of cases where post-processing" \
                 " lost track of code execution"
              << endl;
        stats << "Stack|"
              << max_stack_depth
              << "|Maximum stack depth encountered during post-processing"
              << endl;
        stats << "TSC_TICK|"
              << tsc_tick
              << "|Time Stamp Counter tick"
              << endl;
        stats << "FSB_MHZ|"
              << fsb_mhz
              << "|Front-side Bus MHz"
              << endl;
        if (first_tsc) {
            stats << "first_tsc|"
                  << first_tsc
                  << "|TSC initial offset"
                  << endl;
        }
    }
}
