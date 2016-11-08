#!/usr/bin/env python
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

""" PanicLogger RAM-tracing

"""

import sys
import time
from logger import Logger


class PanicLogger(Logger):
    """ Panic logger
    """
    def __init__(self, control):
        # Base class init call
        Logger.__init__(self, control)

        # Add default kernel module parameter for RAM-tracing
        self._kernel_module_parameters += " trace_method=1 sideband_log_method=1"

        # Add more option to command line input
        self._parser.add_argument('-p', '--panic', action='store', help='Panic tracing mode: 1=Normal, 2=Hooked(default)',
                                  required=False, default=2)
        self._parser.add_argument('-s', '--sideband', action='store', help='Panic tracing mode: 0=Off, 1=On(default)',
                                  required=False, default=1)

        self._parser.add_argument('-g', '--gbuffer', action='store', help='Dump trace data to gbuffer: 0=Off, 1=On(default)',
                                  required=False, default=1)
        self._parser.add_argument('-u', '--userspace', action='store', help='Exclude user space: 0=Off, 1=On(default)',
                                  required=False, default=1)
        self._parser.add_argument('-k', '--kernel', action='store', help='Exclude kernel: 0=Off(default), 1=On',
                                  required=False, default=0)
        self._parser.add_argument('-d', '--dump', action='store',
                                  help='Dump kernel and kernel modules for processing: 0=Off, 1=On(default)',
                                  required=False, default=0)
        self.args = self._parser.parse_args()

        self._kernel_module_parameters += " panic_tracer=" + str(self.args.panic)
        self._kernel_module_parameters += " panic_sideband=" + str(self.args.sideband)
        self._kernel_module_parameters += " panic_gbuffer=" + str(self.args.gbuffer)
        self._kernel_module_parameters += " exclude_userspace=" + str(self.args.userspace)
        self._kernel_module_parameters += " exclude_kernel=" + str(self.args.kernel)

    def initialize(self):
        self._debug_print("PanicLogger::initialize")

        # Initialize Logger base class
        Logger.initialize(self)

        # Call start_tracing earlier to stop execution earlier
        self.start_tracing()

    def start_tracing(self):
        self._debug_print("start_tracing")
        trace_name, trace_path = self.get_trace_name("Enter <<trace name>> to start panic tracing? :")
        if trace_name:
            self.set_trace_path(trace_path, trace_name)

            self.get_build_info()
            # TODO Problem, there is no Sideband.bin info yet
            # Quick Fix
            # Start tracing, wait 100ms, Stop tracing, fetch sideband info
            Logger.start_tracing(self)
            time.sleep(0.2)
            Logger.stop_tracing(self)
            time.sleep(0.2)
            Logger.get_sideband_data(self)

            self.dump_kernel()
            self.dump_linux_gate()
            self.dump_kernel_modules()

            Logger.start_tracing(self)

            print ""
            print "Panic tracing activated"
            print "If panic happens, wait 10s and reboot device."
            print ""
            print "When device boot up run following command:"
            print "sat-panic-fetch " + self.trace_name

            sys.exit(0)
        else:
            print "Panic Tracer did not get started"

    def stop_tracing(self):
        return

    def get_data(self):
        return

    def get_trace_data(self):
        return
