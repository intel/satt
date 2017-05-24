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

""" Target OS awareness
"""

import os
import sys
from satt.common import helper
from satt.common import envstore

os_instance = None


def get_instance():
    global os_instance
    if os_instance is None:
        os = envstore.get_instance().get_variable('sat_os')
        if os < 0:
            print "\nNo target OS selected"
            print "Please run 'satt config'"
            sys.exit(-1)
        else:
            if os == OsHelper.Linux:
                from satt.common.targetos.os_linux import LinuxOs
                os_instance = LinuxOs()
            elif os == OsHelper.Android:
                from satt.common.targetos.os_android import AndroidOs
                os_instance = AndroidOs()
            elif os == OsHelper.ChromeOS:
                from satt.common.targetos.os_chromeos import ChromeOs
                os_instance = ChromeOs()
            elif os == OsHelper.YoctoOs:
                from satt.common.targetos.os_yocto import YoctoOs
                os_instance = YoctoOs()
            else:
                print "ERROR: Unsupported target OS type (" + os + ")"
                sys.exit(-1)
    return os_instance


class OsData:
    Name = ''
    ConnMethods = []
    SourcePathNeeded = False

    def __init__(self, name, connlist, sp):
        self.Name = name
        self.ConnMethods = connlist
        self.SourcePathNeeded = sp


class OsHelper:
    Linux, Android, ChromeOS, YoctoOs = range(4)
    osdata = {Linux: OsData('Linux', ['SSH', 'SHELL'], False),
              Android: OsData('Android', ['ADB'], True),
              ChromeOS: OsData('ChromeOS', ['SSH', 'SHELL'], False),
              YoctoOs: OsData('Yocto', ['SSH'], False)}

class TargetOs(object):
    """ OS specific base class

    """
    _debug = False
    _control = None

    _trace_path = ''
    _trace_binary_path = ''
    _sat_module_paths = ['/tmp/sat.ko']
    _sat_home_path = ''
    _readchar = None
    _helper = None

    def __init__(self, debug=False):
        self._sat_home_path = envstore.store.get_sat_home()
        from satt.common.control import control
        self._control = control.get_instance()
        self._debug = debug
        self._helper = helper.get_instance()
        self._readchar = self._helper.get_readchar_object()

    def get_os_data(self, trace_path):
        self.debug_print("TargetOs::get_os_data")
        self._trace_path = trace_path
        self._trace_binary_path = os.path.join(trace_path, 'binaries')

    def get_tmp_folder(self):
        return '/tmp'

    def get_vmlinux_path(self):
        ''' Virtual function
        '''
    def print_path_type_hint(self, path_type):
        if path_type == 'sat_path_modules':
            print('\n   Hint: folder which contains kernel module binaries under kernel sub folder')
            print('   dir   - kernel')
            print('   file  - modules.dep')
            print('   files - modules.*\n')
        elif path_type == 'sat_path_kernel':
            print('\n   Hint: folder which contains vmlinux and System.map files')
            print('   dir   - arch')
            print('   dir   - drivers')
            print('   dir   - include')
            print('   dir   - kernel')
            print('   file  - System.map')
            print('   file  - vmlinux.*\n')
        elif path_type == 'sat_path_kernel':
            print('\n   Hint: folder which contains vmlinux and System.map files')
            print('   dir   - arch')
            print('   dir   - drivers')
            print('   dir   - include')
            print('   dir   - kernel')
            print('   file  - System.map')
            print('   file  - vmlinux.*\n')
        elif path_type == 'sat_path_kernel_src':
            print('\n   Hint: folder which contains kernel sources')
            print('   dir   - arch')
            print('   dir   - drivers')
            print('   dir   - include')
            print('   file  - Kbuild')
            print('   file  - Kconfig')
            print('   dir   - net')
            print('   dir   - scripts')
        elif path_type == 'sat_target_build':
            print('\n   Hint: folder which contains all other binaries e.g. *.so')
            print('   dir   - etc')
            print('   dir   - lib')
            print('   dir   - usr')
            print('   dir   - var')

    def validate_target_path(self, paths, path_type):
        if path_type in paths:
            paths[path_type] = os.path.abspath(os.path.expanduser(paths[path_type]))

            path_found = os.path.isdir(paths[path_type])
            files_found = False

            if path_type == 'sat_path_modules':
                files_found = os.path.exists(os.path.join(paths[path_type], 'modules.dep'))
            elif path_type == 'sat_path_kernel':
                files_found = os.path.exists(os.path.join(paths[path_type], 'System.map'))
            elif path_type == 'sat_path_kernel_src':
                files_found = os.path.exists(os.path.join(paths[path_type], 'Kconfig'))
            elif path_type == 'sat_target_build':
                files_found = True

            if path_found:
                if files_found:
                    print('   Path found and looks valid!')
                    return True
                else:
                    print(helper.color.BOLD + '   WARNING: Path found, but does not look a valid path!' + helper.color.END)
            else:
                print(helper.color.BOLD + '   ERROR: Path does not found?' + helper.color.END)

            # Pass trought, some path was wrong
            selection = raw_input("   Do you want to give that path again? [Y/n] ")
            if selection == 'Y' or selection == 'y' or selection == None or selection == '':
                return False
            return True

        else:
            print('   ERROR in validate_target_path validation')
            raise

    def get_system_map_path(self):
        ''' Virtual function
        '''

    def is_os(self, os_name):
        self.debug_print("TargetOs::is_os")
        if self.get_name() == os_name:
            return True
        return False

    def copy_binaries(self):
        ''' Virtual function
        '''

    def get_sat_module_paths(self):
        return self._sat_module_paths

    def get_debug_paths(self):
        return ""

    def debug_print(self, string):
        if self._debug:
            print string
