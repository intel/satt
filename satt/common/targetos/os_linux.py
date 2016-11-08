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

""" LinuxOs Class

"""

import os
import re
import sys
import pickle
import platform
from satt.common import helper
from satt.common import envstore
from satt.common.targetos.targetos import TargetOs


class LinuxOs(TargetOs):
    """ Linux specific impl
    """

    def __init__(self):
        # Base class init call
        TargetOs.__init__(self)

# ####################################
# Public methods
# ####################################
    def get_os_data(self, trace_path):
        TargetOs.get_os_data(self, trace_path)
        self.debug_print("LinuxOs::get_os_data")
        self._get_build_info()

    def get_vmlinux_path(self):
        self.debug_print("LinuxOs::get_vmlinux_path")
        kern_ver = self._control.shell_command('uname -r').strip()
        return '/boot/vmlinuz-' + kern_ver

    def get_system_map_path(self):
        self.debug_print("LinuxOs::get_system_map_path")
        kern_ver = platform.release()
        return '/boot/System.map-' + kern_ver

    def get_name(self):
        self.debug_print("LinuxOs::get_name")
        return 'Linux'

    # Methods for CONFIG
    # ##################
    def config(self, variables):
        variables['sat_target_source'] = ''
        variables['sat_target_build'] = '/'
        self._set_sat_kernel_paths(variables)

    def _set_sat_kernel_paths(self, variables):
        print helper.color.BOLD + 'Select kernel paths:' + helper.color.END
        if variables['sat_control_bus'] == 'SHELL':
            # TODO what if SSH command?
            kmods = '/lib/modules/' + platform.release()
            if os.path.exists(kmods):
                selection = raw_input("   Use kernel modules path '" + kmods + "' ? [Y/n] ")
                if selection == 'n' or selection == 'N':
                    kmods = raw_input('Give another kernel modules path: ')
                    variables['sat_path_modules'] = kmods.rstrip()
                    print "'" + kmods + "'"
                else:
                    variables['sat_path_modules'] = kmods
                variables['sat_path_kernel'] = kmods + '/build'
                variables['sat_path_kernel_src'] = variables['sat_path_kernel']
        print '   Target kernel path = ' + helper.color.BOLD + variables['sat_path_kernel'] + helper.color.END
        print
        print 'Auto select ' + helper.color.BOLD + 'Target kernel source path' + helper.color.END
        print '   Target kernel source path = ' + helper.color.BOLD + variables['sat_path_kernel_src'] + helper.color.END
        print
        print 'Auto select ' + helper.color.BOLD + 'Target kernel modules path' + helper.color.END
        print '   Target kernel modules path = ' + helper.color.BOLD + variables['sat_path_modules'] + helper.color.END
        print

# ####################################
# Private methods
# ####################################

    def _get_build_info(self):
        self.debug_print("LinuxOs::_get_build_info")
        build_info = {}
        uname = platform.uname()
        linux_dist = platform.linux_distribution()
        build_info['brand'] = str(linux_dist[0]) + " " + str(linux_dist[1])
        build_info['name'] = uname[1]
        build_info['device'] = uname[1]
        build_info['android_v'] = ''

        build_info['prod_id'] = platform.release()
        build_info['version'] = platform.version()
        build_info['type'] = platform.system()
        build_info['platform'] = str(linux_dist[0]) + " " + str(linux_dist[1]) + " " + str(linux_dist[2])
        build_info['user'] = ''
        build_info['host'] = platform.libc_ver()[0] + " " + platform.libc_ver()[1]
        build_info['kernel_version'] = ' '.join(uname)

        pickle.dump(build_info, open(os.path.join(self._trace_path, "build_info.p"), "wb"), pickle.HIGHEST_PROTOCOL)
        print "Get build info from the device"
