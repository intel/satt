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

""" PTT Logger Implementation

"""

import sys
import os
import re
import time
from logger import Logger
from satt.common import envstore
NAG_OPTIONS = False


class PtiLogger(Logger):
    """ PTI logger
    """

    _mem_buffer_count = 32  # Default Trace buffer allocation 32 x 4MB = 128 MB
    _t32api = None
    _t32_dir = None
    _t32_scripts_directory = None
    _target_platform = None
    _sat_home_path = ''

    def __init__(self, control):
        # Base class init call
        Logger.__init__(control)
        self._sat_home_path = envstore.store.get_sat_home()
        self._t32_dir = os.path.join(self._sat_home_path, 'tracer')
        self._t32_scripts_directory = os.path.join(self._sat_home_path,
            'tracer', 't32_scripts')

        # Add default kernel module parameter for PTI-tracing
        self._kernel_module_parameters += " trace_method=0 "

        # Add more option to command line input
        self._parser.add_argument('-a', '--autofocus', action='store_true',
            help='Run Autofocus before tracing', required=False)
        self._parser.add_argument('-i', '--init', action='store_true',
            help='Init target, configure target for tracing', required=False)
        self._parser.add_argument('-w', '--pti_width', action='store',
            help='Set PTI width to 8,12,16, defaults to 16 bit', required=False)
        self._parser.add_argument('-r','--rawstp', action='store_true',
            help='Fetch raw stp stream', required=False)
        self._parser.add_argument('-s', '--sbpti',
            action='store_true', help='Send Sideband data trought PTI', required=False)
        self._parser.add_argument('-d', '--disable_sleep',
            action='store_true', help='Disable C6 sleep state', required=False)
        self.args = self._parser.parse_args()

        # Initialize T32 API
        try:
            t32 = __import__('satt.trace.t32')
        except ImportError:
            self._t32api = None
        else:
            self._t32api = t32.T32Api()

    def initialize(self):
        self._debug_print("PTILogger::initialize")
        if self._t32api is None:
            print "T32Api not available!"
            sys.exit(1)

        if self.args.disable_sleep:
            print "Disabled C6 sleep state before tracing"
            self._disable_sleep()
        else:
            if NAG_OPTIONS:
                print "C6 sleep state have not been disabled, use -d / --disable_sleep"

        if self.args.sbpti:
            print "Setup Sideband data to come trough PTI"
            self._kernel_module_parameters += "sideband_log_method=0 "
        else:
            self._kernel_module_parameters += "sideband_log_method=1 "

        # Get SoC information
        soc_type = ''

        line = self.control.shell_command("getprop ro.board.platform")
        if not line:
            print "SoC info not found from device!!"
            sys.exit(1)
        else:
            self._target_platform = line.strip()
            print "product: " + self._target_platform
            if self._target_platform == 'platformA':
                soc_type = 'PlatASoC'
            else:
                print "Unsupported SoC type: " + self._target_platform
                sys.exit(1)

        if self.args.init:
            print "Init SoC settings"
            if not self.args.pti_width:
                self.args.pti_width = '16'
                print "pti 16bit"
            elif self.args.pti_width != '4' and self.args.pti_width != '8' and self.args.pti_width != '16':
                print "ERROR: valid values for -w --pti_width are 4,8,16"
                sys.exit()
            else:
                print "pti "+self.args.pti_width+"bit"

            if soc_type == 'PlatASoC':
                self._t32api.run_cmd('Sys.Attach')
                time.sleep(0.2)
                self._t32api.run_cmd('DO '+ os.path.join(self._t32_scripts_directory, 'sat_init_pt_PlatASoC.cmm'))
            else:
                print "run sat_init_ltb.cmm"
                self._t32api.run_cmd('do '+ os.path.join(self._t32_scripts_directory,'sat_init_ltb.cmm ')+soc_type)
                time.sleep(4)
                print "run sat_init_pt.cmm"
                self._t32api.run_cmd('do '+ os.path.join(self._t32_scripts_directory, 'sat_init_pt.cmm ') +
                                     args.pti_width+' '+soc_type+' '+ffd_debug)
                #t32api.run_cmd('do '+self._t32_scripts_directory+'sat_init_pt.cmm')
                time.sleep(4)
        else:
            if NAG_OPTIONS:
                print "Target init configuration was not set, PTI defaults to 16bit, -i 8 to change 8bit"

        if self.args.autofocus:
            print "Running autofocus"
            self._t32api.run_cmd('do '+self._t32_scripts_directory+'sat_autofocus.cmm')
            time.sleep(6)
        else:
            if NAG_OPTIONS:
                print "Autofocus was not done, use -a / --autofocus to do autofocus before tracing"

        if not self.args.sbpti:
            if NAG_OPTIONS:
                print "sideband data trough PTI was not wanted, use -s / --sbpti to enable"

        time.sleep(2)

        if not self._t32api.isrunning:
            raise Exception("T32: System is not running or T32 is not attached to device")

        if self.args.sbpti:
            print "."
            #print self.control.shell_command("""echo 1 > /sys/kernel/debug/sat/sideband_to_pti""")
            print "."

        # Initialize Logger base class
        Logger.initialize()

    def start_tracing(self):
        self._debug_print("start_tracing")
        self._t32api.run_cmd('do ' + os.path.join(self._t32_scripts_directory,
            'sat_start_pt.cmm'))
        Logger.start_tracing()

    def stop_tracing(self):
        self._debug_print("stop_tracing")
        Logger.stop_tracing()
        self._t32api.run_cmd('do '+ os.path.join(self._t32_scripts_directory,
            'sat_stop_pt.cmm'))

    def get_data(self, path, name):
        if path:
            self.set_trace_path(path, name)
            self.get_sideband_data()
            self.get_trace_data()
            self.get_sat_module()
            self._os.get_os_data(self.trace_path)
            self.dump_kernel()
            self.dump_kernel_modules()
            print "All Done!"
            self.instructions_for_processing()

    def get_trace_data(self):
        self._debug_print("get_trace_data")
        # Get RTIT data of each core

        cpu_map = []
        vcpu_mapping = False

        # TODO HowTo check How Many cores from Trace32
        if self.args.sbpti or self.args.rawstp:
            print "PTI trace     : ",
            sys.stdout.flush()
            raw_stma_path = os.path.join(self.trace_path, 'stma.raw')
            self._t32api.run_cmd('a.access denied')
            self._t32api.run_cmd('stma.export.traceport ' + raw_stma_path + ' %BINARY')
            print "stma.raw"
        else:

            self._t32api.run_cmd('a.access denied')

            if self._target_platform:
                #vcpu_map_file = self._t32_dir + '/vcpu_map_' + self._target_platform
                vcpu_map_file = os.path.join(self._t32_dir, 'config', 'vcpu_map_' + self._target_platform)
                if self._debug:
                    print "Opening file:"
                    print vcpu_map_file
                if os.path.isfile(vcpu_map_file):
                    vcpu_mapping = True
                    vcpu_f = open(vcpu_map_file, 'r')
                    while 1:
                        line = vcpu_f.readline().rstrip()
                        if not line:
                            break
                        cpu_items = line.split('=>')
                        cpu_map.insert(int(cpu_items[0]), int(cpu_items[1]))

            # check online cores
            line = self.control.shell_command('cat /sys/devices/system/cpu/online').strip()
            if not line:
                print "ERROR: Getting online CPUs failed!"
                sys.exit(1)
            else:
                vcore = 0
                cores = line.rstrip().split(',')
                if not self._debug:
                    print "Get raw trace from device: ",
                for core in cores:
                    match = re.search("(\d+)-(\d+)", core)
                    if match:
                        for tmpcore in range(int(match.group(1)), int(match.group(2))+1):
                            if vcpu_mapping:
                                vcore = cpu_map[int(tmpcore)]
                            else:
                                vcore = tmpcore
                            if self._debug:
                                print ('a.export.traceport ' + os.path.join(self.trace_path, 'cpu'+str(tmpcore)+'.bin') +
                                       ' /CoreBYTESTREAM /core '+str(vcore))
                            else:
                                print "cpu{0} ".format(str(vcore)),
                            self._t32api.run_cmd('a.export.traceport ' +
                                                 os.path.join(self.trace_path, 'cpu'+str(tmpcore +'.bin') +
                                                 ' /CoreBYTESTREAM /core '+str(vcore)))
                    else:
                        if vcpu_mapping:
                            vcore = cpu_map[int(core)]
                        else:
                            vcore = core
                        if self._debug:
                            print ('a.export.traceport ' + os.path.join(self.trace_path, 'cpu'+str(core)+'.bin') +
                                   ' /CoreBYTESTREAM /core '+str(vcore))
                        else:
                            print "cpu{0} ".format(str(vcore)),
                        self._t32api.run_cmd('a.export.traceport ' + os.path.join(self.trace_path,
                                             'cpu'+str(core)+'.bin') + ' /CoreBYTESTREAM /core '+str(vcore))
                if not self._debug:
                    print ""

    def _disable_sleep(self):
        """ Disable sleep from the Target device
        """
        cpus = self.control.shell_command("grep -c ^processor /proc/cpuinfo")
        for cpu in range(0, int(cpus)):
            self.control.shell_command("echo 1 > /sys/devices/system/cpu/cpu%d/cpuidle/state2/disable"%cpu)
            self.control.shell_command("echo 1 > /sys/devices/system/cpu/cpu%d/cpuidle/state3/disable"%cpu)
            self.control.shell_command("echo 1 > /sys/devices/system/cpu/cpu%d/cpuidle/state4/disable"%cpu)
