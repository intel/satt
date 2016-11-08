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
#include "sat-sideband-model.h"
#include "sat-tid.h"
#include "sat-log.h"
#include "sat-mmapped.h"
#include "sat-file-input.h"
#include "sat-range-map.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <dirent.h>
#include <cinttypes>
#include <algorithm>

using namespace sat;
using namespace std;

namespace sat {

    // TODO: If this class does not get any other members than target path,
    //       it can be replaced with a string. Currently the executable map
    //       is a bit silly, since it is a map from a target path to the
    //       same target path.
    class executable
    {
    public:
        explicit executable(const char* target_path);
        const string& target_path();

    private:
        string target_path_;
    };

}

namespace {

    uint64_t tsc(const sat_header& header)
    {
        return (header.tscp & 0xffffffffff) - header.tsc_offset;
    }

    void print_header(const sat_header& header)
    {
        printf("# %" PRIx64 " (%x)", tsc(header), header.cpu);
    }

    shared_ptr<executable> unmapped = make_shared<executable>("(unmapped)");

    shared_ptr<executable> vdso;
    string                 vdso_path("/linux-gate.so.1");
    const rva              vdso_address(0xffffe000);
    const unsigned         vdso_size(0x1000);

    shared_ptr<path_mapper> host_filesystem;

    // Kernel modules
    struct kernel_module_struct {
        string                 file_name;
        rva                    address;
        rva                    size;
        rva                    offset;
        shared_ptr<executable> exe;
    };
    vector<kernel_module_struct> kernel_modules;

} // anonymous namespace

namespace sat {

    executable::executable(const char* target_path)
        : target_path_(target_path)
    {}

    const string& executable::target_path()
    {
        return target_path_;
    }


    // TODO: make a class for the executable list
    typedef map<string /*target_path*/, shared_ptr<executable>> executable_map;

    executable_map executables;

    shared_ptr<executable> find_executable(const char* target_path)
    {
        shared_ptr<executable> e;
        executable_map::iterator ei(executables.find(target_path));
        if (ei == executables.end()) {
            SAT_LOG(2, "NEW EXECUTABLE MMAP:\n");
            e = make_shared<executable>(target_path);
            executables.insert({target_path, e});
        } else {
            SAT_LOG(2, "KNOWN EXECUTABLE MMAP:\n");
            e = ei->second;
        }

        return e;
    }

    // TODO: remove
    void list_executables()
    {
        for (auto& ei : executables) {
            printf("# %s\n", ei.second->target_path().c_str());
        }
    }


    class mmapping
    {
    public:
        mmapping(rva                    start_in,
                 unsigned               len_in,
                 unsigned               pgoff_in,
                 shared_ptr<executable> exe_in,
                 uint64_t               tsc_in);

        rva                    start;
        unsigned               len;
        unsigned               pgoff;
        shared_ptr<executable> exe;
        uint64_t               tsc;
    };

    mmapping::mmapping(rva                    start_in,
                       unsigned               len_in,
                       unsigned               pgoff_in,
                       shared_ptr<executable> exe_in,
                       uint64_t               tsc_in)
        : start(start_in), len(len_in), pgoff(pgoff_in), exe(exe_in), tsc(tsc_in)
    {
    }


    class mmappings
    {
    public:
        mmappings() : current_tsc_slot_()
        {}

        void dump()
        {
            for (auto& i : mmaps_over_time_) {
                printf("mmap: %10.10lx: [%8.8lx .. %8.8lx]  %s\n",
                       i.first, i.second->start, i.second->start + i.second->len,
                       i.second->exe->target_path().c_str());
            }
        }

        void insert(uint64_t tsc, shared_ptr<mmapping>& m)
        {
            mmaps_over_time_.insert({tsc, m});
            if (tsc >= current_tsc_slot_.first &&
                tsc < current_tsc_slot_.second)
            {
                current_mmaps_.insert({m->start, m->start + m->len}, m);
                current_tsc_slot_.first = tsc;
            }
        }

        bool find(uint64_t tsc, rva address, shared_ptr<mmapping>& m) const
        {
            if (tsc < current_tsc_slot_.first ||
                tsc >= current_tsc_slot_.second)
            {
                // rebuild current mmaps
                current_tsc_slot_.first  = 0;
                current_tsc_slot_.second = numeric_limits<uint64_t>::max();
                for (auto& i : mmaps_over_time_) {
                    if (i.first < tsc) {
                        current_mmaps_.insert({i.second->start,
                                               i.second->start + i.second->len},
                                               i.second);
                        current_tsc_slot_.first = i.first;
                    } else {
                        current_tsc_slot_.second = i.first;
                        break;
                    }
                }
            }

            bool found = current_mmaps_.find(address, m);

#if 0
            if (!found) {
                printf("address %lx not found:\n", address);
                current_mmaps_.iterate([](const pair<rva, rva>& addr, const shared_ptr<mmapping>& m) {
                    printf("    [%lx .. %lx) %s\n",
                           addr.first, addr.second,
                           m->exe->target_path().c_str());
                });
            }
#endif

            return found;
        }

        using callback_func = sideband_model::callback_func;
        void iterate_target_paths(uint64_t tsc, callback_func callback)
        {
            SAT_LOG(3, "ITERATING PATHS UPTO %" PRIx64 "\n", tsc);
            auto e = mmaps_over_time_.upper_bound(tsc);
            for (auto i = mmaps_over_time_.begin(); i != e; ++i) {
                auto m = i->second;
                if (m->exe != unmapped) {
                    if (callback(m->exe->target_path(),
                                 m->start - 0x1000 * m->pgoff))
                    {
                        // the caller found what he was looking for
                        break;
                    }
                }
            }
        }

    private:
        using mmapping_list = map<uint64_t /*tsc*/, shared_ptr<mmapping>>;
        using mmapping_map  = range_map<rva, shared_ptr<mmapping>>;

        mutable mmapping_list            mmaps_over_time_;
        mutable mmapping_map             current_mmaps_;
        mutable pair<uint64_t, uint64_t> current_tsc_slot_;
    }; // class mmappings


    class process
    {
    public:
        explicit process(pid_t p, bool vm=false);

        process(pid_t p, const process& pp);

        void mmap(rva                    start,
                  unsigned               len,
                  unsigned               pgoff,
                  shared_ptr<executable> exe,
                  uint64_t               tsc);
        bool get_mmap(rva      address,
                      uint64_t tsc,
                      string&  path,
                      rva&     start) const;
        using callback_func = sideband_model::callback_func;
        void iterate_target_paths(uint64_t tsc, callback_func callback);

        void name(const char* name, uint64_t tsc);
        const string& name() const;

#if 1
        void print() const
        {
#if 0
            printf("# '%s' pid %u:\n[\n", name_.c_str(), pid);
            for (auto& mi : mmaps_) {
                auto& path = mi.second->exe->target_path();
                if (path != "") {
                    uint64_t tsc   = mi.second->tsc;
                    rva      start = mi.second->start;
                    unsigned len   = mi.second->len;
                    printf("#     %" PRIx64 ": %" PRIx64 "..%" PRIx64 " %s\n", tsc, start, start+len, path.c_str());
                }
            }
            printf("# ]\n");
#endif
        }
#endif

        const pid_t pid;

    private:
        string    name_;
        mmappings mmaps_;
        bool      newly_forked_; // TODO: unnamed_?
    }; // process

    process::process(pid_t p, bool vm)
        : pid(p), name_("<unknown>"), newly_forked_(false)
    {
        if (!vm)
        {
            // by default every process gets mmaps for vdso and kernel modules
            if (!vdso) {
                vdso = make_shared<executable>(vdso_path.c_str());
            }
            {
                auto m = make_shared<mmapping>(vdso_address,
                                               vdso_size,
                                               0,
                                               vdso,
                                               0);
                mmaps_.insert(0, m);
            }

            int index = 1;
            for (auto& km: kernel_modules) {
                if (km.address != 0) {
                    if (!km.exe) {
                        km.exe = make_shared<executable>(km.file_name.c_str());
                        SAT_LOG(1, "MODULE @ %" PRIx64 " size %" PRIx64
                                " offset %" PRIx64 " %s\n",
                                km.address, km.size,
                                km.offset, km.file_name.c_str());
                    }
                    // make kernel module mmap .text and everything before
                    {
                        auto m = make_shared<mmapping>(km.address - km.offset,
                                                       km.size + km.offset,
                                                       0,
                                                       km.exe,
                                                       index);
                        mmaps_.insert(index, m);
                    }
                    ++index;
                }
            }
        } // if (!vm)
    }



    process::process(pid_t p, const process& pp)
        : pid(p), name_(), mmaps_(pp.mmaps_), newly_forked_(true)
    {
        // TODO: set tsc?
    }

    void process::mmap(rva                    start,
                       unsigned               len,
                       unsigned               pgoff,
                       shared_ptr<executable> exe,
                       uint64_t               tsc)
    {
        // TODO: we could insert an unmap the size of the current usespace,
        //       if newly_forked_ here
        auto m = make_shared<mmapping>(start, len, pgoff, exe, tsc);
        mmaps_.insert(tsc, m);
        // assume the first mmap() after fork() is the exec()utable
        if (newly_forked_ && exe != unmapped) {
            name_ = exe->target_path();
            newly_forked_ = false;
        }
#if 0
        if (global_debug_level >= 3) {
            print(); // TODO: remove
        }
#endif
    }

    bool process::get_mmap(rva      address,
                           uint64_t tsc,
                           string&  path,
                           rva&     start) const  // target_load_address
        // TODO: output offset
    {
        shared_ptr<mmapping> m;
        bool found = mmaps_.find(tsc, address, m);
        if (found) {
            path  = m->exe->target_path();
            if (m->pgoff) {
                SAT_LOG(2, "ADJUSTED FOR PGOFF\n");
            }
            start = m->start - 0x1000 * m->pgoff;
        }
        return found;
    }

    void process::iterate_target_paths(uint64_t tsc, callback_func callback)
    {
        mmaps_.iterate_target_paths(tsc, callback);
    }

    void process::name(const char* name, uint64_t tsc)
    {
        name_ = name;
    }

    const string& process::name() const
    {
        return name_;
    }


    typedef map<pid_t /* pid */,
                shared_ptr<process>> pid_map;
    typedef map<pair<unsigned /* cr3 */, uint64_t /* tsc */>,
                shared_ptr<process>> pgd_map;
    typedef map<pid_t /* thread_id */, string> thread_name_map;
    struct scheduling {
        uint32_t rtit_pkt_count;
        pid_t    prev_pid;
        pid_t    prev_thread_id;
        pid_t    pid;
        pid_t    thread_id;
    };
    typedef map<uint64_t /*tsc*/, scheduling> scheduling_map;
    typedef map<unsigned /*cpu*/, scheduling_map> cpu_scheduling_map;

    // TODO: make these non-static member of some class
    vector<pid_t>      all_pids;
    pid_map            pids;
    pgd_map            pgds;
    thread_name_map    thread_names;
    cpu_scheduling_map schedulings;
    struct initial_pid_and_thread_id {
        pid_t    pid;
        pid_t    thread_id;
    };
    map<unsigned /* cpu */, initial_pid_and_thread_id> initial;
    map<unsigned /* cpu */, uint32_t /* pkt_mask */>   pkt_masks;
    uint32_t pkt_mask;

    uint64_t initial_cpu0_tsc;
    unsigned initial_tsc_tick = 0;
    unsigned initial_fsb_mhz  = 0;

    shared_ptr<process> the_process; // for get_target_path()

    // TODO: use some other kind of pointer
    shared_ptr<process> get_process(unsigned cr3, uint64_t tsc)
    {
        auto i = pgds.upper_bound({cr3, tsc});
        if (i != pgds.begin()) {
            --i;
            if (i->first.first == cr3) {
                return i->second;
            }
        }

        return 0;
    }

    shared_ptr<process> get_process(tid_t tid)
    {
        pid_t pid;
        if (tid_get_pid(tid, pid)) {
            auto i = pids.find(pid);
            if (i != pids.end()) {
                return i->second;
            } else {
                if (pid == 0) {
                    // create a process for the swapper
                    shared_ptr<sat::process> p(new sat::process(pid));
                    p->name("swapper", 0);
                    pids.insert({pid, p});
                    return p;
                } else {
                    SAT_LOG(1, "FOUND PID %d, BUT NO PROCESS\n", pid);
                }
            }
        } else {
            SAT_LOG(1, "UNKNOWN TID %u\n", tid);
        }

        return 0;
    }

    class hooking
    {
        public:
            rva    orig;    // address of the hooked function
            rva    copy;    // address of the copy of the hooked function
            rva    end;     // address after the copy of the hooked function
            rva    wrapper; // address of the wrapper function
            string name;    // name of the hooked function

            static rva min_orig;
            static rva max_orig;
            static rva min_copy;
            static rva max_copy;
    };

    vector<hooking> hooks_;

    // statics
    rva hooking::min_orig = 0;
    rva hooking::max_orig = 0;
    rva hooking::min_copy = 0;
    rva hooking::max_copy = 0;

} // namespace sat


class sideband_stdin : public sideband_parser_input {
public:
    bool read(unsigned size, void* buffer)
    {
        auto s = fread(buffer, 1, size, stdin);
        if (s < size) {
            return false; // error: did not get enough data
            // TODO: differentiate between zero and partial reads
        } else {
            return true;
        }
    }

    bool eof() { return feof(stdin); }

    bool bad() { return ferror(stdin); }
};


class sideband_collector : public sideband_parser_output {
public:
        sideband_collector() : done_with_init(false) {}

        virtual void init(const sat_header& header,
                          pid_t             pid,
                          pid_t             thread_id,
                          unsigned          tsc_tick,
                          unsigned          fsb_mhz)
        {
            // collect threads that got executed during the trace
            tid_add(pid, thread_id, header.cpu);
            initial.insert({header.cpu, {pid, thread_id}});
            if (header.cpu == 0) {
                initial_cpu0_tsc = tsc(header);
            }
            initial_tsc_tick = tsc_tick;
            initial_fsb_mhz  = fsb_mhz;
            done_with_init = true;
            SAT_LOG(1, "GOT INITIAL PID: %d, THREAD ID: %u, TSC: %" PRIx64 " TICK: %u FSB %u\n",
                   pid, thread_id, tsc(header), tsc_tick, fsb_mhz);
            //printf("@ ! f %u %u\n", tsc_tick, fsb_mhz);
        };

        virtual void process(const sat_header& header,
                             sat_origin        origin,
                             pid_t             pid,
                             pid_t             ppid,
                             pid_t             tgid,
                             uint64_t          pgd,
                             const char*       name)
        {
            const char* o;
            const char* haba = "TODO"; // TODO: remove

            all_pids.insert(all_pids.end(), pid);
            if (origin == SAT_ORIGIN_INIT) {
                o = "init";
                if (pid == tgid) {
                    // we are getting a process
                    shared_ptr<sat::process> p;
                    pid_map::iterator i(pids.find(pid));
                    if (i == pids.end()) {
                        haba = "A NEW ONE!";
                        p = make_shared<sat::process>(pid);
                        pids.insert({pid, p});
                    } else {
                        p = i->second;
                        haba = "GOT NAME";
                    }
                    auto t = tsc(header);
                    p->name(name, t);
                    pgds.insert({{pgd, t}, p});
                } else {
                    // we are getting a thread; store its name
                    thread_names[pid] = name;
                }
            } else if (origin == SAT_ORIGIN_FORK) {
                o = "fork";
                pid_map::iterator i(pids.find(ppid));
                if (i == pids.end()) {
                    // TODO: should we do something other than discard?
                    SAT_LOG(0, "--- TROUBLE: PARENTLESS FORK (%d/%d <- %d)\n",
                                                             pid, tgid, ppid);
                } else {
                    haba = "GOT FORK";
                    shared_ptr<sat::process> pp;
                    pp = i->second;
                    shared_ptr<sat::process> p(new sat::process(pid, *pp));
                    pids.insert({pid, p});
                    auto t = tsc(header);
                    pgds.insert({{pgd, t}, p});
                    if (global_debug_level >= 3) {
                        p->print(); // TODO: REMOVE
                    }
                }
            } else if (origin == SAT_ORIGIN_SET_TASK_COMM) {
                // we are getting a new name for a thread
                o = "set_task_comm";
                thread_names[pid] = name;
            } else {
                o = "--- process";
            }
            if (global_debug_level >= 2) {
                print_header(header);
                printf("%16s %5u/%5u <- %u, (%" PRIx64 ") %s\n",
                       o, pid, tgid, ppid, pgd, name);
                printf("# %s\n", haba);
            }
        };

        virtual void mmap(const sat_header& header,
                          sat_origin        origin,
                          pid_t             thread_id,
                          uint64_t          start,
                          uint64_t          len,
                          uint64_t          pgoff,
                          const char*       path)
        {
            pid_t       pid;
            const char* o;
            const char* haba = "TODO"; // TODO: remove

            if (origin == SAT_ORIGIN_INIT) {
                // kernel module sends pids for initial mmaps
                pid = thread_id;
                o = "mmap(init)";

                auto e = find_executable(path);

                shared_ptr<sat::process> p;
                pid_map::iterator i(pids.find(pid));
                if (i == pids.end()) {
                    haba = "A NEW ONE!";
                    p = make_shared<sat::process>(pid);
                    pids.insert({pid, p});
                } else {
                    p = i->second;
                    haba = "OLD";
                }
                p->mmap(start, len, pgoff, e, tsc(header));
            } else if (origin == SAT_ORIGIN_MMAP) {
                o = "mmap";
                // kernel module sends pids for mmaps
                pid = thread_id;
                pid_map::iterator i(pids.find(pid));
                if (i == pids.end()) {
                    if (!done_with_init) {
                        SAT_LOG(0, "SAFELY IGNORING mmap() TO PID %d\n", pid);
                    } else {
                        // TODO: what should we do?
                        SAT_LOG(0, "--- TROUBLE: MMAP TO AN UNKNOWN PID %d\n", pid);
                    }
                } else {
                    auto e = find_executable(path);
                    auto p = i->second;
                    p->mmap(start, len, pgoff, e, tsc(header));
                    haba = "MMAP";
                }
            } else if (origin == SAT_ORIGIN_MPROTECT) {
                pid = thread_id; // for debugging printf only
                o = "TODO: mmap(mprotect)";
            } else {
                pid = thread_id; // for debugging printf only
                o = "--- mmap";
            }
            if (global_debug_level >= 2) {
                print_header(header);
                printf("%16s %5u, (%" PRIx64 ", %" PRIx64 ", %" PRIu64 "), %s\n",
                       o, pid, start, len, pgoff, path);
                printf("# %s\n", haba);
            }
        };

        virtual void munmap(const sat_header& header,
                            sat_origin        origin,
                            pid_t             thread_id,
                            uint64_t          start,
                            uint64_t          len)
        {
            pid_t       pid;
            const char* o;

            // kernel module sends pid for munmaps
            pid = thread_id;
            if (origin == SAT_ORIGIN_MUNMAP) {
                o = "munmap";

                pid_map::iterator i(pids.find(pid));
                if (i == pids.end()) {
                    if (!done_with_init) {
                        SAT_LOG(0, "SAFELY IGNORING munmap() to PID %d\n", pid);
                    } else {
                        // TODO: what should we do?
                        SAT_LOG(0, "--- TROUBLE: MUNMAP TO AN UNKNOWN PID %d\n", pid);
                    }
                } else {
                    auto p = i->second;
                    p->mmap(start, len, 0, unmapped, tsc(header));
                }
            } else if (origin == SAT_ORIGIN_MPROTECT) {
                o = "TODO: munmap(mprotect)";
            } else {
                o = "--- munmap";
            }
            if (global_debug_level >= 2) {
                print_header(header);
                printf("%16s %5u, (%" PRIx64 ", %" PRIx64 ")\n", o, pid, start, len);
            }
        };

        virtual void schedule(const sat_header& header,
                              pid_t             prev_pid,
                              pid_t             prev_thread_id,
                              pid_t             pid,
                              pid_t             thread_id,
                              uint32_t          rtit_pkt_count,
                              uint32_t          rtit_pkt_mask,
                              uint64_t          buff_offset)
        {
            // collect threads that got executed during the trace
            tid_add(pid, thread_id, header.cpu);
            schedulings[header.cpu].insert({tsc(header),
                                            { rtit_pkt_count,
                                              prev_pid, prev_thread_id,
                                              pid,      thread_id }});
            // grab the last pkt_mask
            pkt_masks[header.cpu] = rtit_pkt_mask;

            if (global_debug_level >= 2) {
                print_header(header);
                printf("%16s thread_id %u -> %u\n", "schedule()",
                       prev_thread_id, thread_id);
            }
        }

        virtual void hook(const sat_header& header,
                          uint64_t          original_address,
                          uint64_t          new_address,
                          uint64_t          size,
                          uint64_t          wrapper_address,
                          const char*       name)
        {
            rva end = new_address + size;

            hooks_.push_back({original_address,
                              new_address,
                              end,
                              wrapper_address,
                              name});

            if (hooking::min_orig == 0 ||
                hooking::min_orig > original_address)
            {
                hooking::min_orig = original_address;
            }
            if (hooking::max_orig == 0 ||
                hooking::max_orig < original_address)
            {
                hooking::max_orig = original_address;
            }
            if (hooking::min_copy == 0 ||
                hooking::min_copy > new_address)
            {
                hooking::min_copy = new_address;
            }
            if (hooking::max_copy == 0 ||
                hooking::max_copy < end)
            {
                hooking::max_copy = end;
            }
        }

        virtual void module(const sat_header& header,
                            uint64_t          address,
                            uint64_t          size,
                            const char*       name)
        {
            if (host_filesystem) {
                string target_path = string(name) + ".ko";
                string host_path;
                string sym_path;

                if (host_filesystem->find_file(target_path, sym_path, host_path)) {
                    shared_ptr<mmapped> module = mmapped::obtain(host_path, sym_path);
                    rva offset;
                    rva size_ = 0;
                    if (module->get_text_section(offset, size_)) {
                        kernel_modules.push_back({ target_path,
                                                   address,
                                                   size-offset,
                                                   offset });
                        SAT_LOG(1, "kernel module '%s' address %" PRIx64 \
                                ", .text section offset %" PRIx64 \
                                ", size %" PRIx64 "\n",
                                name, address,
                                offset,
                                size-offset);
                    } else {
                        SAT_LOG(0, "ERROR: KERNEL MODULE SECTION: '%s'\n", name);
                    }
                } else {
                    SAT_LOG(0, ".ko file for kernel module '%s' not found\n",
                            name);
                }
            }
        }

private:
        bool done_with_init;
}; // class sideband_collector

// TODO: join with the other namespace sat above
namespace sat {

        void sideband_model::set_vdso_path(const string& path)
        {
            vdso_path = path;
        }

        void sideband_model::set_host_filesystem(
            shared_ptr<path_mapper> filesystem)
        {
            host_filesystem = filesystem;
        }

        bool sideband_model::build(const string& sideband_path)
        {
            vm_sec_list_type vm_section_list;
            return build(sideband_path, vm_section_list);
        }

        bool sideband_model::build(const string& sideband_path, vm_sec_list_type& vm_section_list)
        {
            bool built = false;

            // Create virtual file maps for sections
            vm_sec_list_type fixed_sections;
            for (auto& vm : vm_section_list) {
                fixed_sections[vm.first-vm.second.offset] = {vm.second.size+vm.second.offset, vm.second.offset, vm.second.tid, vm.second.name};
            }

            using sideband_input = file_input<sideband_parser_input>;
            shared_ptr<sideband_input> input{new sideband_input};
            if (input->open(sideband_path)) {
                shared_ptr<sideband_collector>
                    output{new sideband_collector};

                sideband_parser parser(input, output);

                if (parser.parse()) {
                    // Add vm_binaries as processes
                    map<string, unsigned> vm_pids;
                    unsigned i = 1;
                    map<tid_t, bool> process_sent;
                    pid_t vm_pid;
                    for (auto& vm : fixed_sections) {
                        if(!vm_pids.count(vm.second.name)) {
                            shared_ptr<mmapped> vm_map =
                                mmapped::obtain(vm.second.name.c_str(), vm.second.name.c_str(), /* VM */ true);
                            // Find free pid for vm binary
                            {
                                vm_pid = -1;
                                pid_t i;
                                for (i=1; i<0x7FFFFFF0; i++) {
                                    if ( find(all_pids.begin(), all_pids.end(), i) == all_pids.end()) {
                                        vm_pid = i;
                                        break;
                                    }
                                }
                                all_pids.insert(all_pids.end(), vm_pid);
                            }
                            vm_pids[vm.second.name] = vm_pid;
                            shared_ptr<sat::process> p = make_shared<sat::process>(vm_pid, true /*vm*/);
                            pids.insert({vm_pid, p});
                            p->name(vm.second.name.c_str(), i);
                            pgds.insert({{0, i}, p});
                            tid_add(vm_pid, vm_pid, 0);
                        }
                    }

                    // assign tids
                    for (auto& vm : fixed_sections) {
                        tid_get(vm_pids[vm.second.name], 0, vm.second.tid);
                    }

                    {
                        tid_t dummy;
                        tid_get(0, 0, dummy);
                    }

                    // Generate mmaps for vm sections
                    for (auto& vm : fixed_sections) {
                        vm_pid = vm_pids[vm.second.name];

#if 0
                        SAT_LOG(0, "VM_SECTION @ %" PRIx64 " size %" PRIx64 " offset %" PRIx64 " TID:%d %s\n",
                                vm.first,
                                vm.second.size,
                                vm.second.offset,
                                vm.second.tid,
                                vm.second.name.c_str());
#endif
                        auto p = get_process(vm.second.tid);
                        if (p) {
                            auto e = find_executable(vm.second.name.c_str());
                            p->mmap(vm.first,                //rva                    start
                                    vm.second.size,          //unsigned               len,
                                    0,                       //unsigned               pgoff,
                                    e,                       //shared_ptr<executable> exe,
                                    ++i);                    //uint64_t               tsc)
                        } else {
                            SAT_ERR("ERROR: Cant'find process %d\n", vm.second.tid);
                        }
                    }

                    // update tids to original vm_section_list
                    for (auto& vm : vm_section_list) {
                        tid_get(vm_pids[vm.second.name], 0, vm.second.tid);
                    }
                    built = true;
                } else {
                    SAT_ERR("sideband model building failed\n");
                }
            } else {
                SAT_ERR("cannot open sideband file for input: '%s'\n",
                        sideband_path.c_str());
            }

            return built;
        }

        void sideband_model::iterate_schedulings(
                 unsigned cpu,
                 function<void(uint64_t /* tsc */,
                               uint32_t /* pkt_cnt */,
                               tid_t    /* prev tid */,
                               tid_t    /* tid */)> callback) const
        {
            for (auto& i : schedulings[cpu]) {
                tid_t prev_tid;
                tid_t tid;
                if (tid_get(i.second.thread_id, cpu, tid)) {
                    // NOTE: The below if() looks strange, but there
                    // is an explanation: If we are looking at the very first
                    // scheduling for the cpu, and it happened before rtit
                    // tracing started, the thread that got scheduled out
                    // (prev_thread_id) never got a tid assigned for it.
                    // If that happened, the caller is not interested in
                    // the previous thread, but still may need to know
                    // the thread that got scheduled in. Hence we need to
                    // get a false, but safe(!) tid for the thread that
                    // got scheduled out. The only safe value is the tid of
                    // the thread that got scheduled in, so use that as
                    // the false value.
                    if (!tid_get(i.second.prev_thread_id, cpu, prev_tid)) {
                        prev_tid = tid;
                    }
                    callback(i.first,
                             i.second.rtit_pkt_count,
                             prev_tid,
                             tid);
                }
            }
        }

        void sideband_model::set_cr3(unsigned cr3, uint64_t tsc, tid_t tid)
        {
            auto p_by_cr3 = get_process(cr3, tsc);
            auto p_by_tid = get_process(tid);
            if (!p_by_cr3) {
                SAT_LOG(1, "NEW (CR3, TSC) (%x, %" PRIx64 "); ASSUME WE ARE IN EXEC()...\n",
                        cr3, tsc);
                if (p_by_tid) {
                    pgds.insert({{cr3, tsc}, p_by_tid});
                    SAT_LOG(0, "...FOR TID %u\n", tid);
                } else {
                    SAT_LOG(0, "...FOR AN UNKNOWN TID %u\n", tid);
                }
            } else {
                SAT_LOG(1, "KNOWN (CR3, TSC) (%x, %" PRIx64 ")\n",
                        cr3, tsc);
                if (p_by_tid) {
                    if (p_by_cr3->pid != p_by_tid->pid) {
                        SAT_LOG(1, "...BUT PIDS DON'T MATCH: %d != %d\n",
                                p_by_cr3->pid, p_by_tid->pid);
                    }
                } else {
                    SAT_LOG(1, "...BUT UNKNOWN TID %u\n", tid);
                }
            }
        }

        bool sideband_model::get_initial(unsigned  cpu,
                                         tid_t&    tid,
                                         uint32_t& pkt_mask) const
        {
            const auto& m = pkt_masks.find(cpu);
            if (m != pkt_masks.end()) {
                pkt_mask = m->second;
            } else {
                pkt_mask = 2; // default by kernel module
            }

            return tid_get(initial[cpu].thread_id, cpu, tid);
        }

        uint64_t sideband_model::initial_tsc() const
        {
            return initial_cpu0_tsc;
        }

        string sideband_model::process(pid_t pid) const
        {
            string process;

            if (pid == 0) {
                process = "swapper";
            } else {
                auto p = pids.find(pid);
                if (p != pids.end()) {
                    process = p->second->name();
                }
            }

            return process;
        }

        bool sideband_model::get_thread_name(pid_t thread_id, string& name) const
        {
            bool found = false;

            auto t = thread_names.find(thread_id);
            if (t != thread_names.end()) {
                name = t->second;
                found = true;
            }

            return found;
        }

        bool get_target_path(shared_ptr<process> p,
                             rva                 address,
                             uint64_t            tsc,
                             string&             path,
                             rva&                start)
        {
            bool got_it = false;

            if (p->get_mmap(address, tsc, path, start)) {
                got_it = true;
            } else {
                ostringstream p;
                p.setf(ios::showbase);
                p << "TROUBLE: AN UNMAPPED ADDRESS "
                    << hex << address
                    << "; KERNEL MODULE?";
                path = p.str();
                // TODO: what should we do?
            }

            return got_it;
        }

        bool sideband_model::get_target_path(tid_t    tid,
                                             rva      address,
                                             uint64_t tsc,
                                             string&  path,
                                             rva&     start) const
        {
            bool got_it = false;

            auto p = get_process(tid).get();
            if (p) {
                if (p->get_mmap(address, tsc, path, start)) {
                    got_it = true;
                } else {
                    ostringstream p;
                    p.setf(ios::showbase);
                    p << "TROUBLE: AN UNMAPPED ADDRESS "
                        << hex << address
                        << "; KERNEL MODULE?";
                    path = p.str();
                    // TODO: what should we do?
                }
            } else {
                ostringstream s;
                s.setf(ios::showbase);
                s << "NO SUCH PROCESS WITH TID "
                  << hex << tid;
                path = s.str();
            }

            if (got_it) {
                SAT_LOG(1, "GOT PATH %u -> %" PRIx64 "@%s\n",
                        tid, start, path.c_str());
            }

            return got_it;
        }

        bool sideband_model::set_tid_for_target_path_resolution(tid_t tid)
        {
            bool set_it = false;

            if ((the_process = get_process(tid))) {
                set_it = true;
            }

            return set_it;
        }

        bool sideband_model::get_target_path(rva      address,
                                             uint64_t tsc,
                                             string&  path,
                                             rva&     start) const
        {
            bool got_it = false;

            if (the_process) {
                got_it = ::get_target_path(the_process, address, tsc, path,start);
            }

            return got_it;
        }

        void sideband_model::iterate_target_paths(tid_t         tid,
                                                  uint64_t      tsc,
                                                  callback_func callback)
        {
            SAT_LOG(3, "ITERATING PATHS IN %u UPTO %" PRIx64 "\n", tid, tsc);

            auto p = get_process(tid).get();
            if (p) {
                p->iterate_target_paths(tsc, callback);
            }
        }

        void sideband_model::adjust_for_hooks(rva& pc) const
        {
            if (pc >= hooking::min_orig && pc <= hooking::max_orig) {
                // program counter is in the range of hooked functions;
                // see if it maches the start of a hooked function
                for (auto& h : hooks_) {
                    if (pc == h.orig) {
                        // at the start of hooked function; do we have wrapper?
                        if (h.wrapper) {
                            // yes, we have a wrapper; jump to it
                            SAT_LOG(0, "hook: jumping from %s "
                                       "to its wrapper @ %" PRIx64 "\n",
                                    h.name.c_str(), h.wrapper);
                            pc = h.wrapper;
                        } else {
                            SAT_LOG(0, "hook: no wrapper for %s\n",
                                    h.name.c_str());
                        }
                        break;
                    }
                }
            } else if (pc >= hooking::min_copy && pc <= hooking::max_copy) {
                // program counter is in the range of copied functions;
                // see if it is within a copy
                for (auto& h : hooks_) {
                    if (pc >= h.copy && pc < h.end) {
                        // program counter is within the copy;
                        // jump to same offset in the hooked function
                        SAT_LOG(0, "hook: jumping from %s copy to original\n",
                                h.name.c_str());
                        pc = h.orig + (pc - h.copy);
                        break;
                    }
                }
            }
        }

        rva sideband_model::scheduler_tip() const
        {
            rva result = 0;

            for (auto &h : hooks_) {
                if (h.name == "__switch_to") {
                    result = h.copy;
                    break;
                }
            }

            return result;
        }

        unsigned sideband_model::tsc_tick() const
        {
            return initial_tsc_tick;
        }

        unsigned sideband_model::fsb_mhz() const
        {
            return initial_fsb_mhz;
        }

} // namespace sat
