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

# Adb trace control

import re
import sys
import time
from control import Control

DEBUG_ENABLE = False
ADB_API_CHANGE = ('0', '1', '13')


class AdbControl(Control):
    """ Adb Control class

    """
    adb = False

    def __init__(self, debug):
        Control.__init__(self, debug)
        self._debug_print("AdbControl::init")
        try:
            pyadb = __import__('pyadb')
            self.adb = pyadb.ADB('adb')
        except ImportError:
            self.adb = False
        else:
            if not self.adb.check_path():
                print "ERROR: adb was not found"
                sys.exit()
            self.adb_version = re.split('\.', self.adb.PYADB_VERSION)

    def initialize(self):
        if not self.adb:
            print "ADB not supported!"
            sys.exit()
        Control.initialize(self)
        self._debug_print("AdbControl::initialize")
        root = self.adb.shell_command("id")
        if DEBUG_ENABLE:
            print root
        if not root:
            return False

        retry_count = 0
        while (root is not None) and (root.find('uid=0(root)') == -1):
            if retry_count == 0:
                print "set adb to rootmode.."
            root = self.set_adb_root()
            if DEBUG_ENABLE:
                print root
            time.sleep(2)
            root = self.adb.shell_command("id")
            if DEBUG_ENABLE:
                print root
            retry_count += 1
            if retry_count > 3:
                print 'Root mode does not respond'
                print 'Please, try to un-plug and plug the USB cable'
                resp = raw_input("  Press ENTER to continue, 'q' to quit :")
                if resp == 'q' or resp == 'Q':
                    sys.exit(-1)
                retry_count = 1

        if root is not None:
            return True
        else:
            return False

    def shell_command(self, command, skip_exception=False):
        retval = ''
        retval = self.adb.shell_command(command)
        if retval is None:
            retval = ''
        return retval

    def get_remote_file(self, copy_from, copy_to):
        self._debug_print("AdbControl::get_file {0} to {1}".format(copy_from, copy_to))
        return self.adb.get_remote_file(copy_from, copy_to)

    def set_adb_root(self):
        if self.adb.set_adb_root.func_code.co_argcount == 1:
            return self.adb.set_adb_root()
        else:
            return self.adb.set_adb_root(1)

    def push_local_file(self, copy_from, copy_to):
        self._debug_print("AdbControl::push_file {0} to {1}".format(copy_from, copy_to))
        return self.adb.push_local_file(copy_from, copy_to)

    def get_tmp_folder(self):
        return '/data'
