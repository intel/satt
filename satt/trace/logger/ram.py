# -*- coding: utf-8 -*-
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

""" RamLogger Class

"""

import sys
import os
import re
from logger import Logger
from satt.common import envstore


class RamLogger(Logger):
    """ RAM logger
    """

    _mem_buffer_count = 32  # Default Trace buffer allocation 32 x 4MB = 128 MB

    def __init__(self, control):
        # Base class init call
        Logger.__init__(self, control)
        self._sat_home_path = envstore.store.get_sat_home()
        # Add default kernel module parameter for -tracing
        self._kernel_module_parameters += " trace_method=1 sideband_log_method=1"

        # Add more option to command line input
        self._parser.add_argument('-b', '--buffers', action='store',
                                  help='Amount of RTIT buffers to use', required=False)
        self.args = self._parser.parse_args()

        if self.args.buffers:
            self._kernel_module_parameters += " max_trace_buffers=" + self.args.buffers

    def initialize(self):
        self._debug_print("RamLogger::initialize")

        # Initialize Logger base class
        Logger.initialize(self)

    def start_tracing(self):
        self._debug_print("start_tracing")
        Logger.start_tracing(self)

    def stop_tracing(self):
        self._debug_print("stop_tracing")
        Logger.stop_tracing(self)

    def get_data(self, path, name):
        if path:
            self.set_trace_path(path, name)
            self.get_sideband_data()
            self.get_trace_data()
            self.get_sat_module()
            self._os.get_os_data(self.trace_path)
            self.dump_kernel()
            # self.dump_linux_gate()
            self.dump_kernel_modules()
            # ci.packageForUpload(name)
            print "All Done!"
            # ci.instructions_for_processing(name, SAT_HOME, path)
            self.instructions_for_processing()

    def get_trace_data(self):
        self._debug_print("get_trace_data")
        # Get RTIT data of each core
        trace_streams = self._control.shell_command("ls /sys/kernel/debug/sat/*_stream")
        trace_streams = trace_streams.strip().split()

        off_file = open(os.path.join(self.trace_path, "cpu_offsets.txt"), "w")
        for cpu_stream in trace_streams:
            match = re.search("/cpu(\d+)_stream", cpu_stream)
            if match:
                print "\rcpu" + match.group(1) + " rtit data: ",
                sys.stdout.flush()
                self._control.get_remote_file(cpu_stream.strip(), self.trace_path + "/cpu" + match.group(1) + ".bin")
                off_file.write(self._control.shell_command("cat /sys/kernel/debug/sat/cpu" + match.group(1) + "_offset"))
                print cpu_stream
        off_file.close()
