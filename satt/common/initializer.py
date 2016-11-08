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

import os
import sys
import glob
import subprocess
from satt.common import envstore


class Satt:

    _usage_str = ''
    _sat_path = ''
    _satt_version = ''
    _variables = None
    _options = {}

    def __init__(self, sat_path, sat_venv_bin):
        self._sat_path = sat_path
        self._sat_venv_bin = sat_venv_bin
        envstore.get_instance().set_sat_home(sat_path)
        envstore.get_instance().set_sat_venv_bin(sat_venv_bin)

    def initialize(self):
        envstore.get_instance().load()
        self.check_version()

    def check_version(self):
        version_file = os.path.join(self._sat_path, '.version')
        self._satt_version = ''

        if os.path.isfile(version_file):
            self._satt_version = open(version_file).readline().rstrip()
        else:
            old_dir = os.getcwd()
            os.chdir(self._sat_path)
            try:
                self._satt_version = subprocess.check_output('git describe --tags --always', shell=True).rstrip()
            except:
                self._satt_version = '0.0.0'
            os.chdir(old_dir)

        envstore.get_instance().set_sat_version(self._satt_version)

    def parse_options(self):
        if len(sys.argv) > 1:
            if sys.argv[1] == "--version" or sys.argv[1] == "-v":
                print ("satt version: " + self._satt_version)
                sys.exit(0)
            if sys.argv[1] == '--completewords':
                print (' '.join(sorted(self._options)))
                sys.exit(0)
            if sys.argv[1] == '--home':
                print (self._sat_path)
                sys.exit(0)

    def get_commands(self):
        opt_desc = {}

        # glob all commands.py files
        for root, dirs, files in os.walk(os.path.join(self._sat_path, 'satt')):
            cmd_file = glob.glob(os.path.join(root, 'command.py'))
            if len(cmd_file) > 0:
                key = os.path.basename(os.path.dirname(cmd_file[0]))
                self._options[key] = 'satt.' + key
                f = open(cmd_file[0])
                while True:
                    line = f.readline()
                    if not line or line.startswith('import'):
                        break
                    if line.startswith('# description: '):
                        opt_desc[key] = line.split(':')[1]

        # Create USAGE string
        self._usage_str = ('\nUSAGE: satt [-v|--version] [command]\n Commands:\n')
        for k in sorted(self._options.keys()):
            self._usage_str += '     ' + k.ljust(15) + ': '
            if k in opt_desc.keys():
                self._usage_str += opt_desc[k]
            else:
                self._usage_str += '\n'

        # return commands
        return self._options, opt_desc

    def print_usage(self):
        print (self._usage_str)
