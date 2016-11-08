#!/usr/bin/env python
'''
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
'''

from satparser import *
from collections import defaultdict
import sys
import os
if sys.platform.startswith('win'):
    import msvcrt


class Enum(set):
    def __getattr__(self, name):
        if name in self:
            return name
        raise AttributeError

SAT_ORIGIN_INIT = 1
SAT_ORIGIN_FORK = 2
SAT_ORIGIN_MMAP = 3
SAT_ORIGIN_MPROTECT = 4
SAT_ORIGIN_MUNMAP = 5
SAT_ORIGIN_SET_TASK_COMM = 6


def print_header(header):
    print ("%x(-%x) (%x)" % (header['tscp'] & 0xffffffffff, header['tsc_offset'], header['cpu'],)),

'''
class sideband_parser_output:
    def init(self, header, pid, tid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq):
        return

    def process(self, header, origin, pid, ppid, tgid, pgd, name):
        return

    def mmap(self, header, origin, pid, start, leng, pgoff, path):
        return

    def munmap(self, header, origin, pid, start, leng):
        return

    def schedule(self, header, prev_pid, prev_tid, pid, tid, ipt_pkt_count, ipt_pkt_mask, buff_offset, schedule_id):
        return

    def schedule_id(self, header, addr, id):
        return

    def hook(self, header, original_address, new_address, size, wrapper_address, name):
        return

    def module(self, header, address, size, name):
        return

    def generic(self, header, name, data):
        return

    def codedump(self, header, addr, size, name):
        return

    def header_and_binary(self, header, binary):
        return
'''

class sideband_stdin(sideband_parser_input):
    # c:\ type mydoc.txt | python.exe -u myscript.py
    last_len_ = 0

    def __init__(self):
        if sys.platform.startswith('win'):
            msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)

    def read(self, size):
        try:
            buffer = sys.stdin.read(size)
            self.last_len_ = len(buffer)
            if size != self.last_len_:
                self.last_len_ = 0
                return False
            return buffer
        except:
            return False

    def eof(self):
        return False if self.last_len_ else True

    def bad(self):
        # return ferror(stdin)
        return False


# class sideband_dumper(sideband_parser_output):
class sideband_dumper:
    addr_ = 0

    def __init__(self, mmap_address_to_find):
        self.addr_ = mmap_address_to_find

    def init(self, header, pid, tid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq):
        print_header(header)
        print "           INIT (%5u, %5u) TSC tick=%u FSB MHz=%u TSC=%u CTC=%u MTC freq=%u" % \
        (pid, tid, tsc_tick, fsb_mhz, tma_ratio_tsc, tma_ratio_ctc, mtc_freq, )

    def process(self, header, origin, pid, ppid, tgid, pgd, name):
        if origin == SAT_ORIGIN_INIT:
            o = "init"
        elif origin == SAT_ORIGIN_FORK:
            o = "fork"
        elif origin == SAT_ORIGIN_SET_TASK_COMM:
            o = "set_task_comm"
        else:
            o = "--- process"

        print_header(header)
        print "%15s %5u/%5u <- %u, (%x) %s" % (o, pid, tgid, ppid, pgd, name,)

    def mmap(self, header, origin, pid, start, leng, pgoff, path):
        if origin == SAT_ORIGIN_INIT:
            o = "mmap(init)"
        elif origin == SAT_ORIGIN_MMAP:
            o = "mmap"
        elif origin == SAT_ORIGIN_MPROTECT:
            o = "mmap(mprotect)"
        else:
            o = "--- mmap"

        print_header(header)
        print "%15s %5u, (%x, %x, %x) [%x..%x] %s%s" % \
              (o, pid, start, leng, pgoff, start, start + leng - 1, path,
               " MATCH" if (start <= self.addr_ and self.addr_ < start + leng) else "", )

    def munmap(self, header, origin, pid, start, leng):
        if origin == SAT_ORIGIN_MUNMAP:
            o = "munmap"
        elif origin == SAT_ORIGIN_MPROTECT:
            o = "munmap(mprotect)"
        else:
            o = "--- munmap"

        print_header(header)
        print "%15s %5u, (%x, %x)" % (o, pid, start, leng,)

    def schedule(self, header, prev_pid, prev_tid, pid, tid, ipt_pkt_count, ipt_pkt_mask, buff_offset, schedule_id):
        print_header(header)
        print "      schedule %u @ %08x (b-off: %9lx) (mask %1x); (%5u, %5u) -> (%5u, %5u)" % (
               schedule_id, ipt_pkt_count, buff_offset, ipt_pkt_mask, prev_pid, prev_tid, pid, tid)

    def schedule_id(self, header, addr, id):
        print_header(header)
        print "  schedule_id_%d @ %08x" %(id, addr)

    def hook(self, header, original_address, new_address, size, wrapper_address, name):
        print_header(header)
        print("            hook %08x -> %08x (%5u bytes) %s, %08x" %
              (original_address, new_address, size, name, wrapper_address))

    def module(self, header, address, size, name):
        print_header(header)
        print("          module @ %08x, %8d: %s" % (address, size, name,))

    def generic(self, header, name, data):
        printf("%x sb generic %s: %s" % (header['tscp'] & 0xffffffffff, name, data,))

    def codedump(self, header, addr, size, name):
        print_header(header)
        print("        codedump @ %08x, %8d: %s" % (addr, size, name,))

dump_mode = Enum(['none', 'traced', 'all'])


class path_dumper(sideband_parser_output):
    mode_ = 0
    paths_ = []

    def __init__(self, mode):
        self.mode_ = mode

    def mmap(self, header, origin, pid, start, len, pgoff, path):
        if path:
            if origin != SAT_ORIGIN_INIT or self.mode_ == dump_mode.all:
                self.paths_.append(path)

    def printp(self):
        for path in self.paths_:
            print "{0}".format(path)


class generic_dumper(sideband_parser_output):
    first_tsc_ = 0
    output_path_ = ''
    msgs_ = []

    def __init__(self, first_tsc, output_path):
        self.fist_tsc_ = first_tsc
        self.output_path_ = output_path

    def generic(self, header, name, data):
        buffer = "%lu|%s\n" % ((header['tscp'] & 0xffffffffff) - self.first_tsc_, data)
        self.msgs_.append(buffer)

    def write_messages(self):
        # @TODO
        # typedef map<string, msg_it
        # typedef list<string>.const_iterator data_it
        # file_ext = ".satg"
        # for msg in self.msgs_:
        #     output.open(self.output_path_ + file_ext + self.msgs_[0])
        #     if not output
        # for(msg = msgs_.begin(); msg != msgs_.end(); msg++)                ofstream output
        #     output.open(output_path_ + file_ext + msg.first)
        #     if not output:                    fprintf(stderr, "could not open '%s' for writing\n", (output_path_ + file_ext + msg.first).c_str())
        #         sys.exit(EXIT_FAILURE)

        # for (data = msg.second.begin(); data != msg.second.end(); ++data)                    output << data.c_str()

        #    output.close()
        return
        # first_tsc_
        # output_path_
        # map<string, msgs_


class schedule_dumper(sideband_parser_output):
    output_path_ = None
    started_ = 0
    start_thread_ = {}
    epoc_ = 0
    processes_ = {}
    threads_ = {}
    output_path_
    per_cpu_outputs_ = {}

    def __init__(self, output_path):
        self.output_path_ = output_path
        self.processes_[0] = "kernel"

    def init(self, header, pid, tid, tsc_tick, fsb_mhz):
        self.started_ = True
        self.collect_header_info(header)
        self.threads_[tid] = pid
        self.start_thread_[header['cpu']] = tid
        for t in self.start_thread_:
            # self.output_schedule(self.epoc_, t.first, t.second)
            self.output_schedule(self.epoc_, header['cpu'], tid)

    def process(self, header, origin, pid, ppid, tgid, pgd, name):
        self.collect_header_info(header)
        self.processes_[pid] = name
        # printf("%u: process (%u, %s)\n", (unsigned)header.tscp, pid, name)

    def mmap(self, header, origin, pid, start, len, pgoff, path):
        if pid not in self.processes_.keys():
            self.processes_[pid] = path

    def schedule(self, header, prev_pid, prev_tid, pid, tid, ipt_pkt_count, ipt_pkt_mask, buff_offset, schedule_id):
        self.collect_header_info(header)
        self.threads_[tid] = pid
        if not self.started_:
            self.start_thread_[header['cpu']] = tid
        else:
            self.output_schedule(header['tscp'], header['cpu'], tid)

    def schedule_id(self, header, addr, id):
        return


    def dump(self):
        self.close_per_cpu_outputs()
        self.dump_process_names()

    # private:
    def collect_header_info(self, header):
        if self.started_:
            if not self.epoc_ or header['tscp'] < self.epoc_:
                if self.epoc_:
                    sys.stderr.write("WARNING: non-linear timestamp detected\n")
                self.epoc_ = header['tscp']

    def output_schedule(self, tsc, cpu, tid):
        if cpu not in self.per_cpu_outputs_:
            path = self.output_path_ + ".cs" + str(cpu)
            f = open(path, "w")
            if not f:
                sys.stderr.write("ERRROR: cannot open %s for writing\n" % (str(path)))
                sys.exit(EXIT_FAILURE)
            else:
                self.per_cpu_outputs_[cpu] = f

        try:
            record = struct.pack('<II', tsc-self.epoc_, tid)
        except:
            # @ TODO
            print tsc
            print self.epoc_
            print (tsc-self.epoc_) & 0xffffffff
            return
        if self.per_cpu_outputs_[cpu].write(record) != None:
            sys.stderr.write("ERROR: cannot write to output file\n")
            sys.exit(-7)

    def dump_process_names(self):
        path = self.output_path_ + ".satt"
        f = open(path, "wb")
        if not f:
            sys.stderr.write("ERRROR: cannot open %s for writing\n" % (path.str()))
            sys.exit(EXIT_FAILURE)

        for key, value in self.threads_.iteritems():
            tid = key
            pid = value
            name = self.processes_[pid]

            name_len = len(name)
            record = struct.pack('<II'+str(name_len)+'s', tid, pid, name)
            if f.write(record) != None:
                sys.stderr.write("ERROR: cannot write to output file\n")
                sys.exit(-6)

    def close_per_cpu_outputs(self):
        for o in self.per_cpu_outputs_:
            if self.per_cpu_outputs_[o].close():
                sys.stderr.write("WARNING: error closing cpu %u output\n" % o)


class module_dumper(sideband_parser_output):
    def module(self, header, address, size, name):
        print_header(header)
        print("          module @ %08x, %8d: %s" % (address, size, name,))


class codedump_dumper(sideband_parser_output):
    def codedump(self, header, addr, size, name):
        print_header(header)
        print("        codedump @ %08x, %8d: %s" % (addr, size, name,))


def usage(name):
    sys.stderr.write("Usage: %s [address]\n"
                     "       %s -p\n"
                     "       %s -P\n"
                     "       %s -s path\n"
                     "       %s -g first_tsc\n" % (name, name, name, name, name))


def main(argv):
    input = sideband_stdin()
    generic_msgs = None
    schedules = None
    module = None
    paths = None
    output = None
    mmap_address_to_find = 0
    output_path = None
    first_tsc = 0

    i = 0
    for arg in argv:
        i += 1
        arg = str(arg)

        if arg == "-p":
            output = paths = path_dumper(dump_mode.traced)
        elif arg == "-P":
            output = paths = path_dumper(dump_mode.all)
        elif arg == "-s":
            ++i
            if i < len(argv):
                output = schedules = schedule_dumper(argv[i])
            else:
                usage(sys.argv[0])
                sys.exit(-1)
        elif arg == "-m":
            output = module = module_dumper()
        # ''' Generic sideband message dump '''
        elif arg == "-c":
            output = module = codedump_dumper()
        elif arg == "-g":
            ++i
            if i < argv:
                first_tsc = int(argv[i]) % 2**32
            else:
                usage(sys.argv[0])
                sys.exit(-2)

        elif arg == "-o":
            ++i
            if i < argv:
                output_path = sys.argv[i]
            else:
                usage(argv[0])
                sys.exit(-3)
        # @TODO --- what is this
        # elif sscanf(argv[i], "%" PRIx64, &mmap_address_to_find) != 1:
        #  usage(argv[0])
        #  sys.exit(EXIT_FAILURE)

    if output_path != "" and first_tsc != 0:
        output = generic_msgs = generic_dumper(first_tsc, output_path)

    if not output:
        output = sideband_dumper(mmap_address_to_find)

    parser = sideband_parser(input, output)

    parsing_ok = parser.parse()

    if paths:
        paths.printp()

    if schedules:
        schedules.dump()

    if generic_msgs:
        generic_msgs.write_messages()

    if parsing_ok:
        if not module:
            print "OK"
        sys.exit(0)
    else:
        print "ERROR"
        sys.exit(-5)

if __name__ == "__main__":
    main(sys.argv)
