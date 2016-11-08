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

""" Logger Class

"""

import time
import sys
import os
import re

import multiprocessing
import argparse
import shutil
from satt.common import envstore
from satt.common.targetos import targetos
from satt.common.sbparser import sbcombiner


class Logger(object):
    """ Base Logger class
    """
    # Public
    sat_home_path = ""

    trace_name = ""
    trace_path = ""
    trace_binary_path = ""

    args = ""

    # Protected
    _parser = ""
    _power_period = False
    _kernel_module_parameters = ""
    _debug = False
    _active_sat_module_path = ""
    _control = None
    _os = None

    # Private
    _path = ''
    _local_build_path = ''
    _variables = None

    def _debug_print(self, msg):
        if self._debug:
            print msg

    def __init__(self, control):
        self.sat_home_path = envstore.store.get_sat_home()
        self._variables = envstore.store.get_current()

        self._debug_print("Logger::init")
        self._control = control
        self._parser = argparse.ArgumentParser(description='sat-trace to trace your system')
        self._parser.add_argument('-c', '--power', action='store',
                                  help='Read current and voltage -p <polling period ms>', required=False)
        self._parser.add_argument('--debug', action='store_true',
                                  help='Debug prints', required=False)
        self._parser.add_argument('-m', help='Trace mode', required=False)

        self._satt_venv_bin = envstore.store.get_sat_venv_bin()
        # get instance to os specific object:
        self._os = targetos.get_instance()

        # Initialize controller
        if not self._control.initialize():
            print "ERROR: trace device not found, or more than one device connected"
            sys.exit(-1)

    def initialize(self):
        """ Initalize Trace setup
             - load kernel module
        """
        self._debug_print("Logger::initialize")

        # Common kernel module command line handler
        if self.args.power:
            if 0 < int(self.args.power) <= 1000:
                self._power_period = self.args.power
                self._kernel_module_parameters += " power_monitor=" + self.args.power

        if self.args.debug:
            self._debug = True

        # Load SAT-kernel module
        self._load_kernel_module()

    def _copy_and_load_kernel_module(self):
        ret = False
        local_sat_path = os.path.join(self.sat_home_path, 'src', 'kernel-module', 'sat.ko')
        if os.path.exists(local_sat_path):
            print "Copy sat.ko from sat env to target device"
            self._control.push_local_file(local_sat_path, self._os.get_tmp_folder() + '/sat.ko')
            try:
                try_data_folder = self._control.shell_command("insmod " + self._os.get_tmp_folder() + "/sat.ko sat_path=" +
                                                              self._os.get_tmp_folder() + "/sat.ko " +
                                                              self._kernel_module_parameters)
                if try_data_folder is None:
                    ret = True
            except Exception as e:
                print "ERROR: " + str(e)
                sys.exit(1)
        return ret

    def _load_kernel_module(self):
        """ Load kernel module
        """
        print
        lsmod = self._control.shell_command("lsmod")
        if lsmod is None:
            lsmod = ""
        if "sat " not in lsmod:
            time.sleep(1)
            print "install sat.ko (" + self._kernel_module_parameters + ")"
            result = False
            for path in self._os.get_sat_module_paths():
                res = self._control.shell_command('if [ -e "' + path + '" ]; then echo OK; fi')
                if "OK" in res:
                    res = self._control.shell_command("insmod " + path + " sat_path=" +
                                                      path + " " + self._kernel_module_parameters)
                else:
                    continue
                if res != '':
                    if "can't open" in res or "No such file" in res:
                        continue
                    else:
                        print "ERROR: " + res
                        sys.exit(1)
                else:
                    result = True
                    self._active_sat_module_path = path
                    print "Loaded " + path + " kernel module"
                    break
            if not result:
                # SAT ko not found from device
                # Try to copy to device
                if not self._copy_and_load_kernel_module():
                    print "sat.ko loading failed"
                    sys.exit()
        else:
            if self._power_period:
                print self._control.shell_command("echo " + self._power_period + " > /sys/module/sat/parameters/power_monitor")

    def start_tracing(self):
        ''' Start Tracing, base implementation
        '''
        self._debug_print("start_tracing")
        res = self._control.shell_command("echo 1 > /sys/kernel/debug/sat/trace_enable")
        if res == '':
            print "Started"
        else:
            print "Error: {0}".format(res)
            sys.exit(-1)

    def stop_tracing(self):
        ''' Stop Tracing, base implementation
        '''
        self._debug_print("stop_tracing")
        return self._control.shell_command("echo 0 > /sys/kernel/debug/sat/trace_enable")

    def get_trace_name(self, msg=""):
        if not msg:
            msg = "Save Trace by giving <<trace name>> or discard trace by pressing <<enter>>? :"
        trace_name = raw_input(msg)
        if trace_name:
            # Save the Traces
            path = os.getcwd()
            trace_path = os.path.join(path, trace_name)

            if not os.path.exists(trace_path):
                os.makedirs(trace_path)
            return trace_name, trace_path
        return False, False

    def set_trace_path(self, trace_path, trace_name=""):
        ''' Set Trace input path
        '''
        self.trace_path = trace_path
        self.trace_name = trace_name
        self.trace_binary_path = os.path.join(trace_path, 'binaries')

    def get_per_cpu_sideband(self):
        ''' Get per cpu sideband files and combine into one sideband.bin
        '''
        self._debug_print("get_per_cpu_sideband")
        sb_files = self._control.shell_command('ls /sys/kernel/debug/sat/cpu*_sideband').strip().split('\n')
        cpu_sb_paths = []
        combined_sb = os.path.join(self.trace_path, 'sideband.bin')
        for f in sb_files:
            self._control.get_remote_file(f, os.path.join(self.trace_path, os.path.basename(f)))
            cpu_sb_paths.append(os.path.join(self.trace_path, os.path.basename(f)))
        sbcomb = sbcombiner.sideband_combiner(cpu_sb_paths, combined_sb)
        sbcomb.combine()

    def get_sideband_data(self):
        ''' Get Sideband Data, base implementation
        '''
        self._debug_print("get_sideband_data")

        # Get sideband info
        print "\rsideband data : ",
        sys.stdout.flush()
        # self._control.shell_command("cat /sys/kernel/debug/sat/sideband_data > " + self._tmp + "/sideband.bin")
        if 'OK' in self._control.shell_command('if [ -e "/sys/kernel/debug/sat/sideband_data" ]; then echo OK; fi'):
            self._control.get_remote_file('/sys/kernel/debug/sat/sideband_data', os.path.join(self.trace_path, 'sideband.bin'))
            print "/sys/kernel/debug/sat/sideband_data"
        elif 'OK' in self._control.shell_command('if [ -e "/sys/kernel/debug/sat/cpu0_sideband" ]; then echo OK; fi'):
            self.get_per_cpu_sideband()
            print "/sys/kernel/debug/sat/cpuX_sideband"
        else:
            print "NOT FOUND!"


    def get_sat_module(self):
        # Get sat module from device to trace folder
        binary_path = os.path.join(self.trace_path, 'binaries')
        if not os.path.isdir(binary_path):
            os.makedirs(binary_path)
        if self._active_sat_module_path == "":
            # p = os.popen('adb shell "cat /sys/module/sat/parameters/sat_path"')
            # line = p.readline().rstrip()
            sat_kernel_module_path = self._control.shell_command("cat /sys/module/sat/parameters/sat_path").strip()
            if sat_kernel_module_path.find("sat.ko") >= 0:
                self._active_sat_module_path = sat_kernel_module_path
            else:
                for mpath in self._sat_module_paths:
                    if 'OK' in self._control.shell_command('if [ -e "' + mpath + '" ]; then echo OK; fi'):
                        self._active_sat_module_path = line
                        break

        print "\rsat.ko        : ",
        sys.stdout.flush()
        self._control.get_remote_file(self._active_sat_module_path, os.path.join(binary_path, 'sat.ko'))
        print self._active_sat_module_path

    def get_trace_data(self):
        ''' Virtual function, implement as needed
        '''
        self._debug_print("get_trace_data")

    def get_data(self):
        ''' Virtual function, implement as needed
        '''

    def get_sideband_module_info(self, modules):
        try:
            python_path = 'python'

            if self._satt_venv_bin:
                python_path = os.path.join(self._satt_venv_bin, python_path)
            cmd = python_path + ' ' + os.path.join(self.sat_home_path, 'satt', 'common', 'sbparser', 'sbdump.py')
            subp = os.popen(cmd + " -m < " + os.path.join(self.trace_path, "sideband.bin"))
            if subp:
                while True:
                    line = subp.readline()
                    if not line:
                        break
                    match = re.search("module @ ([0-9a-fA-F]+),\s+([0-9]+): (\S+)", line)
                    if match:
                        modules[match.group(3)] = ["0x"+match.group(1), match.group(2)]
            subp.close()
        except:
            print "Warning: Kernel modules dumping failed", sys.exc_info()[0]

    def dump_kernel_modules(self):
        # Dump kernel binary from the phone memory
        dev_list = {}

        local_kmod_path = os.path.join(self.trace_binary_path, 'dump', 'kmod')
        self.get_sideband_module_info(dev_list)

        if not os.path.exists(local_kmod_path):
            os.makedirs(local_kmod_path)

        index_count = 0
        for name, data in dev_list.items():
            index_count += 1
            print '\rDump kernel modules: ' + str(index_count * 100 / len(dev_list)).rjust(3, ' ') + '%',
            sys.stdout.flush()
            ko_fetch_addr_cmd = 'echo {0} > /sys/kernel/debug/sat/ko_fetch_addr'.format(data[0])
            self._control.shell_command(ko_fetch_addr_cmd)
            ko_fetch_addr_cmd = 'echo {0} > /sys/kernel/debug/sat/ko_fetch_size'.format(data[1])
            self._control.shell_command(ko_fetch_addr_cmd)
            # self._control.shell_command("cat /d/sat/ko_fetch_data > " + self._os.get_tmp_folder() + "/dump3")
            self._control.get_remote_file('/sys/kernel/debug/sat/ko_fetch_data', os.path.join(local_kmod_path, name + ".dump"))
        print

    def dump_kernel(self):
        print "\rFetch Kernel dump:     0%",
        sys.stdout.flush()

        kernel_address = ""
        kernel_size = ""
        python_path = 'python'

        if self._satt_venv_bin:
            python_path = os.path.join(self._satt_venv_bin, python_path)
        sub_python = os.popen(python_path + ' ' + os.path.join(self.sat_home_path,
                              'satt', 'common', 'sbparser', 'sbdump.py') + ' -c < ' +
                              os.path.join(self.trace_path, "sideband.bin"))
        if sub_python:
            while True:
                line = sub_python.readline()
                if not line:
                    break
                match = re.search("codedump @ ([0-9a-fA-F]+),\s+(\d+): (\S+)", line)
                if match:
                    if match.group(3).find("vmlinux") >= 0:
                        kernel_address = "0x" + match.group(1)
                        kernel_size = match.group(2)
        sub_python.close()

        kernel_dump_path = os.path.join(self.trace_binary_path, 'dump')
        if not os.path.exists(kernel_dump_path):
            os.makedirs(kernel_dump_path)

        print "\rFetch Kernel dump:     5%",
        sys.stdout.flush()
        # Dump kernel code into file
        kernel_dump_cmd = "echo {0} > /sys/kernel/debug/sat/ko_fetch_addr".format(kernel_address)
        self._control.shell_command(kernel_dump_cmd)
        kernel_dump_cmd = "echo {0} > /sys/kernel/debug/sat/ko_fetch_size".format(kernel_size)
        self._control.shell_command(kernel_dump_cmd)
        print "\rFetch Kernel dump:    20%",
        sys.stdout.flush()
        self._control.get_remote_file('/sys/kernel/debug/sat/ko_fetch_data', os.path.join(kernel_dump_path, 'kernel_dump'))
        print "\rFetch Kernel dump:   100%"
        sys.stdout.flush()

#    def pack_for_upload(self, name):
#        if self._os.is_official_build():
#            package = raw_input("All done <<enter>> to exit or <<U>> to create upload " +
#                                name + ".tgz package? :")
#            if package == 'U' or package == 'u':
#                call("tar cvzf " + name + ".tgz " + "./" + name, shell=True)
#                print("creating " + name + ".tgz package with cmd " + "tar cvzf " +
#                      name + ".tgz " + "./" + name)
#                raw_input("All done, you can now upload " + os.getcwd() + "/" +
#                          name + ".tgz package to sat2.tm.intel.com server")

    def dump_linux_gate(self):
        outp = self._control.shell_command('dd if=/proc/self/mem bs=4096 skip=1048574 count=1 of=' +
                                           self._os.get_tmp_folder() + '/linux-gate.so.1')
        print "\rDumping linux-gate:   0%",
        sys.stdout.flush()
        self._control.get_remote_file(self._os.get_tmp_folder() + '/linux-gate.so.1',
                                      os.path.join(self.trace_path, 'linux-gate.so.1'))
        print "\rDumping linux-gate: 100%"

    def instructions_for_processing(self):
        if sys.platform.startswith('win'):

            print "Post process traces by running:"
            print "#> satt process {0}".format(self.trace_name)
        else:
            print "Post process traces, by run following command:"
            print "#> satt process {0}".format(self.trace_name)
            print "\nProcess info - TODO"
