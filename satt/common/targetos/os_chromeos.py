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

""" ChromeOs Class

"""

import os
import re
import sys
from satt.common import envstore
from satt.common.targetos.targetos import TargetOs


class ChromeOs(TargetOs):
    """ Chrome OS specific impl
    """

    def __init__(self):
        # Base class init call
        TargetOs.__init__(self)

# ####################################
# Public methods
# ####################################
    def get_os_data(self, trace_path):
        TargetOs.get_os_data(self, trace_path)
        self.debug_print("ChromeOs::get_os_data")
        self._get_build_info()

    def get_vmlinux_path(self):
        # TODO: Find out where to get vmlinux file
        self.debug_print("ChromeOs::get_vmlinux_path")
        return 'vmlinux'

    def get_system_map_path(self):
        # TODO: Find out where to get System.map file
        self.debug_print("ChromeOs::get_system_map_path")
        return 'System.map'

    def get_name(self):
        self.debug_print("ChromeOs::get_name")
        return 'ChromeOs'

    # Methods for CONFIG
    # ##################
    def config(self, variables):
        variables['sat_target_source'] = ''
        variables['sat_target_build'] = '/'
        variables['sat_path_kernel'] = '/'
        variables['sat_path_kernel_src'] = variables['sat_path_kernel']
        variables['sat_path_modules'] = '/'

# ####################################
# Private methods
# ####################################

    def _get_build_info(self):
        self.debug_print("ChromeOs::_get_build_info")
        # TODO: How to get ChromeOs build info
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
