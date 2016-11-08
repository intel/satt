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

############################################
# description: Development tools
############################################

import os
import sys
import argparse
import subprocess
from satt.common import envstore

##################################
# Command file for satt build
##################################
class_name = "SattDevel"

# private
satt_devel_help_scr_size = 18
satt_devel_help_indent_size = 22


class HelpTextFormatter(argparse.HelpFormatter):
    def _split_lines(self, text, width):
        if text.startswith('#'):
            return text[1:].splitlines()
        return argparse.HelpFormatter._split_lines(self, text, width)


class ScriptData:
    Desc = ''
    Cmd = ''

    def __init__(self, desc, cmd):
        self.Desc = desc
        self.Cmd = cmd


class SattDevel:
    _sat_home = ''
    _variables = {}
    _scripts = {}

    def __init__(self):
        self._sat_home = envstore.store.get_sat_home()
        self._variables = envstore.store.get_current()
        param1 = ''
        params = []
        if len(sys.argv) > 2:
            param1 = sys.argv[2]
        if len(sys.argv) > 3:
            params = sys.argv[3:]
        self._scripts = {'compile-parser': ScriptData('Compile post-process parser fron source code\n',
                                                      (os.path.join(self._sat_home, 'satt', 'devel', 'compile_parser.py') +
                                                       ' ' + param1 + ' '.join(params) )),
                         'build-ui': ScriptData('Build SATT UI\n',
                                                      (os.path.join(self._sat_home, 'satt', 'devel', 'build_ui.py') +
                                                       ' ' + param1 + ' '.join(params) )),
                         'command': ScriptData('possibility to call sat-xxx commands directly\n',
                                               (os.path.join(self._sat_home, 'satt', 'process', 'bin', 'x86_64', param1) +
                                                ' ' + ' '.join(params))),
                         'pack-binaries': ScriptData('Pack object files executed in the target during\n' +
                                                     ' ' * satt_devel_help_indent_size + ' the trace into tgz package\n',
                                                     (os.path.join(self._sat_home, 'satt', 'devel', 'pack-binaries') +
                                                      ' ' + param1 + ' '.join(params) )),
                         'pack-trace': ScriptData('Pack raw satt trace files into tgz package\n' +
                                                  ' ' * satt_devel_help_indent_size + ' call sat-rtit-dump etc directly\n',
                                                  (os.path.join(self._sat_home, 'satt', 'devel', 'pack-trace') +
                                                   ' ' + param1 + ' '.join(params) ))}

    def action(self):
        # Complete words for bash autocomplete
        if len(sys.argv) > 1:
            if sys.argv[1] == '--completewords':
                print ' '.join(self._scripts.keys())
                sys.exit(0)

        parser = argparse.ArgumentParser(description='satt devel', formatter_class=HelpTextFormatter)
        help_txt = '#Devel script to run:\n'
        for s in sorted(self._scripts.keys()):
            help_txt += '  ' + s.ljust(satt_devel_help_scr_size) + ': ' + self._scripts[s].Desc
        parser.add_argument('script', action='store', help=help_txt)
        self._args, additionals = parser.parse_known_args()

        if self._args.script in self._scripts.keys():
            os.system(self._scripts[self._args.script].Cmd)
        else:
            print "Unknown command: '" + self._args.script + "'"
            sys.exit(-1)
