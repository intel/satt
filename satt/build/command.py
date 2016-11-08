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

##################################################################
# description: Build kernel module against the target kernel build
##################################################################

import os
import sys
import argparse
import subprocess
from satt.common import envstore
from satt.common.control import AdbControl
from satt.common.control import SshControl
from satt.common.control import ShellControl
from satt.common.targetos import targetos

##################################
# Command file for satt build
##################################
class_name = "SattBuildModule"


class SattBuildModule:
    _sat_home = ''
    _variables = {}
    _control = None

    def __init__(self):
        self._sat_home = envstore.store.get_sat_home()
        self._variables = envstore.store.get_current()

    def action(self):
        if self._variables['sat_path_kernel'] == '':
            print "No kernel path set.\nPlease call 'satt config' first."
            print
            sys.exit(-1)

        parser = argparse.ArgumentParser(description='satt build')
        parser.add_argument('-l', '--load', action='store_true',
                            help='Only load kernel-module into device', required=False)
        self._args = parser.parse_args()

        # SATT_CONTROL_BUS
        if self._variables['sat_control_bus'] == "SSH":
            self._control = SshControl(False)
        elif self._variables['sat_control_bus'] == "SHELL":
            self._control = ShellControl(False)
        else:  # self._variables['sat_control_bus'] == "ADB":
            self._control = AdbControl(False)

        if not self._args.load:
            print 'Make SATT kernel-module'
            os.chdir(os.path.join(self._sat_home, 'src', 'kernel-module'))
            os.environ['SAT_TARGET_BUILD'] = self._variables['sat_target_build']
            os.environ['SAT_TARGET_SOURCE'] = self._variables['sat_target_source']
            os.environ['SAT_PATH_KERNEL'] = self._variables['sat_path_kernel']
            os.environ['SAT_TARGET_DEV'] = self._variables['sat_path_kernel_src']
            subprocess.call('make clean', shell=True)
            subprocess.call('./build.sh', shell=True)

        if self._control.initialize():
            kmod_path = os.path.join(self._sat_home, 'src', 'kernel-module', 'sat.ko')
            if os.path.isfile(kmod_path):
                target_os = targetos.get_instance()
                print '\nCopy SATT kernel-module into target device'
                self._control.push_local_file(kmod_path,  target_os.get_tmp_folder() + '/sat.ko')
                try:
                    self._control.shell_command('rmmod sat', True)
                except:
                    pass
