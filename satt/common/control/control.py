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

# Trace control

import sys

DEBUG = False
control_instance = None


def get_instance():
    global control_instance
    if control_instance is None:
        from satt.common import envstore
        ctrl = envstore.get_instance().get_variable('sat_control_bus')
        if ctrl == '':
            print "\nNo control bus selected"
            print "Please run 'satt config'"
            sys.exit(-1)
        else:
            if ctrl == "SSH":
                from satt.common.control import SshControl
                control_instance = SshControl(DEBUG)
            elif ctrl == "SHELL":
                from satt.common.control import ShellControl
                control_instance = ShellControl(DEBUG)
            elif ctrl == "ADB":
                from satt.common.control import AdbControl
                control_instance = AdbControl(DEBUG)
            else:
                print "ERROR: Unsupported control bus type (" + ctrl + ")"
                sys.exit(-1)
    return control_instance


class Control(object):
    """ Trace Control base class

    """
    _debug = False
    # Logger instance
    # TODO this will be changed to own command line handler class
    _logger = None

    def __init__(self, debug):
        self._debug = debug

    # Convert input data to string
    def _normalize_shell_output(self, data):
        if data == None or data == '':
            return ''
        # is string
        elif isinstance(data, basestring):
            return data
        # is iterable
        elif isinstance(data, list):
            return ''.join(data)
        else:
            print 'ERROR: unknown return value'

    def _debug_print(self, msg):
        if self._debug:
            print msg

    def initialize(self, logger=None):
        """ initialize
        """
        self._logger = logger

    def shell_command(self, command, skip_exception=False):
        """ run_command
        """
        print "run_command"

    def get_remote_file(self, copy_from, copy_to):
        """ get_file
        """
        print "get_file"

    def push_local_file(self, copy_from, copy_to):
        """ push_file
        """
        print "push_file"

    def get_tmp_folder(self):
        """ push_file
        """
        print "get_tmp_folder"
