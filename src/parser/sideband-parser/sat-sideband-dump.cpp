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
#include "sat-sideband-parser.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <set>
#include <map>
#include <list>
#include <sstream>
#include <cstring>
#include <cinttypes>

using namespace sat;
using namespace std;

namespace {

    void print_header(const sat_header& header)
    {
        printf("%" PRIx64 "(-%x) (%x)", header.tscp & 0xffffffffffff, header.tsc_offset, header.cpu);
    }

}


class sideband_stdin : public sideband_parser_input {
public:
    bool read(unsigned size, void* buffer) override
    {
        auto s = fread(buffer, 1, size, stdin);
        if (s < size) {
            return false; // error: did not get enough data
            // TODO: differentiate between zero and partial reads
        } else {
            return true;
        }
    }

    bool eof() override { return feof(stdin); }

    bool bad() override { return ferror(stdin); }
};


class sideband_dumper : public sideband_parser_output {
public:
        sideband_dumper(uint64_t mmap_address_to_find) :
            addr_(mmap_address_to_find)
        {}

        virtual void init(const sat_header& header,
                          pid_t             pid,
                          pid_t             tid,
                          unsigned          tsc_tick,
                          unsigned          fsb_mhz,
                          uint32_t          tma_ratio_tsc,
                          uint32_t          tma_ratio_ctc,
                          uint8_t           mtc_freq) override
        {
            print_header(header);
            printf("            INIT (%5u, %5u) TSC tick=%u FSB MHz=%u TSC=%u CTC=%u MTC freq=%u\n",
              pid, tid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq);
        };
        virtual void process(const sat_header& header,
                             sat_origin        origin,
                             pid_t             pid,
                             pid_t             ppid,
                             pid_t             tgid,
                             uint64_t          pgd,
                             const char*       name) override
        {
            const char* o;
            if (origin == SAT_ORIGIN_INIT) {
                o = "init";
            } else if (origin == SAT_ORIGIN_FORK) {
                o = "fork";
            } else if (origin == SAT_ORIGIN_SET_TASK_COMM) {
                o = "set_task_comm";
            } else {
                o = "--- process";
            }
            print_header(header);
            printf("%16s %5u/%5u <- %u, (%" PRIx64 ") %s\n",
                   o, pid, tgid, ppid, pgd, name);
        };
        virtual void mmap(const sat_header& header,
                          sat_origin        origin,
                          pid_t             pid,
                          uint64_t          start,
                          uint64_t          len,
                          uint64_t          pgoff,
                          const char*       path) override
        {
            const char* o;
            if (origin == SAT_ORIGIN_INIT) {
                o = "mmap(init)";
            } else if (origin == SAT_ORIGIN_MMAP) {
                o = "mmap";
            } else if (origin == SAT_ORIGIN_MPROTECT) {
                o = "mmap(mprotect)";
            } else {
                o = "--- mmap";
            }
            print_header(header);
            printf("%16s %5u, (%" PRIx64 ", %" PRIx64 ", %" PRIx64 ") [%" PRIx64 "..%" PRIx64 "] %s%s\n",
                   o, pid, start, len, pgoff, start, start + len - 1, path,
                   (start <= addr_ && addr_ < start + len) ? " MATCH" : "");
        };
        virtual void munmap(const sat_header& header,
                            sat_origin        origin,
                            pid_t             pid,
                            uint64_t          start,
                            uint64_t          len) override
        {
            const char* o;
            if (origin == SAT_ORIGIN_MUNMAP) {
                o = "munmap";
            } else if (origin == SAT_ORIGIN_MPROTECT) {
                o = "munmap(mprotect)";
            } else {
                o = "--- munmap";
            }
            print_header(header);
            printf("%16s %5u, (%" PRIx64 ", %" PRIx64 ")\n", o, pid, start, len);
        };
        virtual void schedule(const sat_header& header,
                              pid_t             prev_pid,
                              pid_t             prev_tid,
                              pid_t             pid,
                              pid_t             tid,
                              uint32_t          ipt_pkt_count,
                              uint32_t          ipt_pkt_mask,
                              uint64_t          buff_offset,
                              uint8_t           schedule_id) override
        {
            print_header(header);
            printf("       schedule %d @ %08x (b-off:%10lx) (mask %1x); (%5u, %5u) -> (%5u, %5u)\n",
                   schedule_id, ipt_pkt_count, buff_offset, ipt_pkt_mask, prev_pid, prev_tid, pid, tid);
        };

        virtual void schedule_idm(const sat_header& header,
                              uint64_t          address,
                              uint8_t           id) override
        {
            print_header(header);
            printf("  schedule_id_%d @ %08" PRIx64 "\n", id, address);
        };

        virtual void hook(const sat_header& header,
                          uint64_t          original_address,
                          uint64_t          new_address,
                          uint64_t          size,
                          uint64_t          wrapper_address,
                          const char*       name) override
        {
            print_header(header);
            printf("            hook %08" PRIx64 " -> %08" PRIx64 " (%5" PRIu64 " bytes) %s, %08" PRIx64 "\n",
                   original_address, new_address, size, name, wrapper_address);
        }
        virtual void module(const sat_header& header,
                            uint64_t          address,
                            uint64_t          size,
                            const char*       name) override
        {
            print_header(header);
            printf("          module @ %08" PRIx64 ", %8" PRId64 ": %s\n", address, size, name);
        }

        virtual void generic(const sat_header& header,
                             const char*       name,
                             const char*       data) override
        {
            printf("%" PRIx64 " sb generic %s: %s\n", header.tscp & 0xffffffffff, name, data);
        };

        virtual void codedump(const sat_header& header,
                              uint64_t          addr,
                              uint64_t          size,
                              const char*       name) override
        {
            print_header(header);
            printf("        codedump @ %08" PRIx64 ", %8" PRId64 ": %s\n", addr, size, name);
        };
private:
        uint64_t addr_;

};


class path_dumper : public sideband_parser_output {
public:
        typedef enum { none, traced, all } dump_mode;

        explicit path_dumper(int mode) : mode_(mode) {}

        virtual void mmap(const sat_header& header,
                          sat_origin        origin,
                          pid_t             pid,
                          uint64_t          start,
                          uint64_t          len,
                          uint64_t          pgoff,
                          const char*       path) override
        {
            if (path && *path) {
                if (origin != SAT_ORIGIN_INIT || mode_ == all) {
                    paths_.insert(path);
                }
            }
        };

        void print()
        {
            for (auto& path : paths_) {
                printf("%s\n", path.c_str());
            }
        }

private:
        int         mode_;
        set<string> paths_;
};

class generic_dumper : public sideband_parser_output {
public:
        explicit generic_dumper(uint64_t first_tsc, string output_path) :
            first_tsc_(first_tsc), output_path_(output_path) {}
        virtual void generic(const sat_header& header,
                            const char*       name,
                            const char*       data) override
        {
            char buffer[256];
            sprintf(buffer, "%lu|%s\n",(header.tscp & 0xffffffffff) - first_tsc_, data);
            msgs_[name].push_back(buffer);
        };

        void write_messages(void) {
            typedef map<string,list<string>>::const_iterator msg_it;
            typedef list<string>::const_iterator data_it;
            const string file_ext = ".satg";
            for(msg_it msg = msgs_.begin(); msg != msgs_.end(); msg++) {
                ofstream output;
                output.open(output_path_ + file_ext + msg->first);
                if (!output) {
                    fprintf(stderr, "could not open '%s' for writing\n", (output_path_ + file_ext + msg->first).c_str());
                    exit(EXIT_FAILURE);
                }
                for (data_it data = msg->second.begin(); data != msg->second.end(); ++data) {
                    output << data->c_str();
                }
                output.close();
            }
        }

private:
        uint64_t                  first_tsc_;
        string                    output_path_;
        map<string, list<string>> msgs_;
};

class schedule_dumper : public sideband_parser_output {
public:
        schedule_dumper(string output_path)
            : output_path_(output_path)
        {
            processes_[0] = "kernel";
        }

        virtual void init(const sat_header& header,
                          pid_t             pid,
                          pid_t             tid,
                          unsigned          tsc_tick,
                          unsigned          fsb_mhz,
                          uint32_t          tma_ratio_tsc,
                          uint32_t          tma_ratio_ctc,
                          uint8_t           mtc_freq) override
        {
            started_ = true;
            collect_header_info(header);
            threads_[tid] = pid;
            start_thread_[header.cpu] = tid;
            for (auto& t : start_thread_){
                output_schedule(epoc_, t.first, t.second);
            }
        };
        virtual void process(const sat_header& header,
                             sat_origin        origin,
                             pid_t             pid,
                             pid_t             ppid,
                             pid_t             tgid,
                             uint64_t          pgd,
                             const char*       name) override
        {
            collect_header_info(header);
            processes_[pid] = name;
            //printf("%u: process (%u, %s)\n", (unsigned)header.tscp, pid, name);
        };
        virtual void mmap(const sat_header& header,
                          sat_origin        origin,
                          pid_t             pid,
                          uint64_t          start,
                          uint64_t          len,
                          uint64_t          pgoff,
                          const char*       path) override
        {
            if (processes_[pid] == "") {
                processes_[pid] = path;
            }
        };
        virtual void schedule(const sat_header& header,
                              pid_t             prev_pid,
                              pid_t             prev_tid,
                              pid_t             pid,
                              pid_t             tid,
                              uint32_t          ipt_pkt_count,
                              uint32_t          ipt_pkt_mask,
                              uint64_t          buff_offset,
                              uint8_t           schedule_id) override
        {
            collect_header_info(header);
            threads_[tid] = pid;
            if (!started_) {
                start_thread_[header.cpu] = tid;
            } else {
                output_schedule(header.tscp, header.cpu, tid);
            }
        };

        void dump()
        {
            close_per_cpu_outputs();
            dump_process_names();
        }

private:
        void collect_header_info(const sat_header& header)
        {
            if (started_) {
                if (!epoc_ || header.tscp < epoc_) {
                    if (epoc_) {
                        fprintf(stderr, "WARNING: non-linear timestamp detected\n");
                    }
                    epoc_ = header.tscp;
                }
            }
        }

        void output_schedule(uint64_t tsc, unsigned cpu, pid_t tid)
        {
#if 0
            pid_t pid = threads_[tid];
            printf("%10u, cpu %u: %u (%u, %s)\n",
                   (unsigned)(tsc - epoc_),
                   cpu,
                   tid,
                   pid,
                   processes_[pid].c_str());
#endif
            FILE* f = per_cpu_outputs_[cpu];
            if (!f) {
                ostringstream path;
                path << output_path_ << ".cs" << cpu;
                f = fopen(path.str().c_str(), "w");
                if (!f) {
                    fprintf(stderr, "ERRROR: cannot open %s for writing\n",
                            path.str().c_str());
                    exit(EXIT_FAILURE);
                } else {
                    per_cpu_outputs_[cpu] = f;
                }
            }

            struct { uint32_t tsc; uint32_t tid; } record =
                { (uint32_t)(tsc - epoc_), (uint32_t)tid };
            if (fwrite(&record, sizeof(record), 1, f) != 1) {
                fprintf(stderr, "ERROR: cannot write to output file\n");
                exit(EXIT_FAILURE);
            }
        }

        void dump_process_names()
        {
#if 0
            for (auto& p : processes_) {
                printf("%u -> %s\n", p.first, p.second.c_str());
            }
#endif

            ostringstream path;
            path << output_path_ << ".satt";
            FILE* f = fopen(path.str().c_str(), "w");
            if (!f) {
                fprintf(stderr, "ERRROR: cannot open %s for writing\n",
                        path.str().c_str());
                exit(EXIT_FAILURE);
            }

            for (auto& t : threads_) {
                pid_t tid = t.first;
                pid_t pid = t.second;
                string& name = processes_[pid];

                struct { uint32_t tid; uint32_t pid; char name[256]; } record =
                    { (uint32_t)tid, (uint32_t)pid };
                memset(record.name, 0, sizeof(record.name));
                strncpy(record.name, name.c_str(), sizeof(record.name)-1);

                if (fwrite(&record, sizeof(record), 1, f) != 1) {
                    fprintf(stderr, "ERROR: cannot write to output file\n");
                    exit(EXIT_FAILURE);
                }
            }
        }

        void close_per_cpu_outputs()
        {
            for (auto& o : per_cpu_outputs_) {
                if (fclose(o.second) != 0) {
                    fprintf(stderr, "WARNING: error closing cpu %u output\n",
                            o.first);
                }
            }
        }

        bool                 started_;
        map<unsigned, pid_t> start_thread_; // cpu -> tid
        uint64_t             epoc_;
        map<pid_t, string>   processes_; // pid -> process name
        map<pid_t, pid_t>    threads_;   // tid -> pid
        string               output_path_;
        map<unsigned, FILE*> per_cpu_outputs_;
};


void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s [address]\n"
            "       %s -p\n"
            "       %s -P\n"
            "       %s -s path\n"
            "       %s -g first_tsc\n",
            name, name, name, name, name);
}

int main(int argc, char* argv[])
{
    shared_ptr<sideband_parser_input>  input(new sideband_stdin);
    shared_ptr<sideband_parser_output> output;
    shared_ptr<path_dumper>            paths;
    shared_ptr<schedule_dumper>        schedules;
    shared_ptr<generic_dumper>         generic_msgs;
    uint64_t                           mmap_address_to_find = 0;
    string                             output_path;
    uint64_t                           first_tsc = 0;

    for (int i = 1; i < argc; ++i) {
        string arg(argv[i]);

        if (arg == "-p") {
            output = paths = make_shared<path_dumper>(path_dumper::traced);
        } else if (arg == "-P") {
            output = paths = make_shared<path_dumper>(path_dumper::all);
        } else if (arg == "-s") {
            ++i;
            if (i < argc) {
                output = schedules = make_shared<schedule_dumper>(argv[i]);
            } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        /* Generic sideband message dump */
        } else if (arg == "-g") {
            ++i;
            if (i < argc) {
                first_tsc = strtoul (argv[i], NULL, 0);
            } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else if (arg == "-o") {
            ++i;
            if (i < argc) {
                output_path = argv[i];
            } else {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else if (sscanf(argv[i], "%" PRIx64, &mmap_address_to_find) != 1) {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (output_path != "" and first_tsc != 0) {
        output = generic_msgs = make_shared<generic_dumper>(first_tsc, output_path);
    }

    if (!output) {
        output = make_shared<sideband_dumper>(mmap_address_to_find);
    }

    sideband_parser parser(input, output);

    bool parsing_ok = parser.parse();

    if (paths) {
        paths->print();
    }
    if (schedules) {
        schedules->dump();
    }
    if (generic_msgs) {
        generic_msgs->write_messages();
    }
    if (parsing_ok) {
        printf("OK\n");
        exit(EXIT_SUCCESS);
    } else {
        printf("ERROR\n");
        exit(EXIT_FAILURE);
    }
}
