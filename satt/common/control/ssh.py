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

# Ssh trace control

import sys
import subprocess
from control import Control
from satt.common import envstore


class SshControl(Control):
    """ SSH Control class

    """
    _ip_address = None
    _ssh_command = "ssh"
    _scp_command = "scp"

    def __init__(self, debug, ip_address=None):
        self._debug_print("SshControl::init")
        Control.__init__(self, debug)
        self._ip_address = ip_address
        if sys.platform.startswith('win'):
            print "Windows SSH not support yet!"
            sys.exit(-1)
            # self._ssh_command = "plink.exe"
            # self._scp_command = "pscp.exe"

    def initialize(self):
        Control.initialize(self)
        self._ip_address = envstore.store.get_variable('sat_control_ip')
        self._debug_print("SshControl::initialize")
        # Initialize Logger base class
        return True

    def _check_ip_address(self):
        if self._ip_address is None:
            self._ip_address = envstore.store.get_variable('sat_control_ip')

    def shell_command(self, command, skip_exception=False):
        self._check_ip_address()
        self._debug_print("SshControl::shell_command")
        ssh = subprocess.Popen([self._ssh_command, "%s" % self._ip_address, command],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               shell=False)
        res = ssh.stdout.readlines()
        return self._normalize_shell_output(res)

    def get_remote_file(self, copy_from, copy_to):
        self._check_ip_address()
        self._debug_print("SshControl::get_remote_file")

        p = subprocess.Popen([self._ssh_command, "%s" % (self._ip_address,), "cat %s" % (copy_from,)],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              shell=False)
        res = p.stdout.read()
        open(copy_to, "wb").write(res)
        return 0

    def push_local_file(self, copy_from, copy_to):
        self._check_ip_address()
        self._debug_print("SshControl::push_local_file")
        return 0

    def get_tmp_folder(self):
        return '/tmp'
