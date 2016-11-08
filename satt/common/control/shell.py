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

# Shell trace control

import sys
import shutil
import subprocess
from control import Control


class ShellControl(Control):
    """ Shell Control class

    """

    def __init__(self, debug):
        self._debug_print("ShellControl::init")
        Control.__init__(self, debug)

    def initialize(self):
        Control.initialize(self)
        self._debug_print("ShellControl::initialize")
        # TODO: set to root?
        return True

    def shell_command(self, command, skip_exception=False):
        self._debug_print("ShellControl::shell_command")
        if skip_exception:
            return subprocess.check_output(command, shell=True)
        else:
            try:
                return subprocess.check_output(command, shell=True)
            except Exception as e:
                return str(e)

    def get_remote_file(self, copy_from, copy_to):
        self._debug_print("ShellControl::get_remote_file")
        return shutil.copyfile(copy_from, copy_to)

    def push_local_file(self, copy_from, copy_to):
        self._debug_print("ShellControl::push_remote_file")
        return shutil.copyfile(copy_from, copy_to)

    def get_tmp_folder(self):
        return '/tmp'
