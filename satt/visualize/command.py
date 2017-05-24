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

###########################################
# description: Visualize traces in SATT UI
###########################################

import os
import sys
import signal
import argparse
import subprocess
import webbrowser
from satt.common import envstore

class_name = "SattVisualize"
# satt visualize <trace_name> --remote

backend_handle = None


def signal_handler(sigid, frame):
    global backend_handle
    if backend_handle:
        os.killpg(backend_handle.pid, signal.SIGTERM)
    sys.exit(0)


class SattVisualize:
    sat_home = ''
    _args = None
    url = ''
    _outfile = None

    def __init__(self):
        self.sat_home = envstore.store.get_sat_home()
        self.satt_venv_bin = envstore.store.get_sat_venv_bin()
        os.environ['SAT_HOME'] = self.sat_home
        self.chrome_path = '/usr/bin/google-chrome %s'

    def action(self):
        global backend_handle

        parser = argparse.ArgumentParser(description='satt visualize')
        parser.add_argument('PATH', nargs='?', action='store', help='(optional) Trace folder to visualize')
        parser.add_argument('-r', '--remote', action='store_true',
                            help='use remote sat server', required=False)
        parser.add_argument('-i', '--importonly', action='store_true',
                            help='import only, do not show SAT UI', required=False)
        self._args = parser.parse_args()
        if self._args.importonly and not self._args.PATH:
            print "Error: Path must be defined in 'import only' case"
            return

        if self._args.remote:
            # send data into remote sat server
            sat_gui_server = 'your . gui . server . com'
            sat_upload_path = '/upload/'
            sat_tgz_file = self._args.PATH
            self.url = "http://" + sat_gui_server
            if self._args.PATH:
                if '.tgz' not in self._args.PATH:
                    sat_tgz_file += '-sat.tgz'
                if os.path.isfile(sat_tgz_file):
                    key_path = os.path.join(self.sat_home, 'satt', 'visualize', 'backend', 'keys', 'sat-upload-rsa')
                    if subprocess.check_output('stat -c "%a" ' + key_path, shell=True) != "600":
                        subprocess.call('chmod 600 ' + key_path, shell=True)
                    subprocess.call('scp -i ' + key_path + ' ' + sat_tgz_file + ' sat@' +
                                    sat_gui_server + ':' + sat_upload_path, shell=True)
                    print "Upload done, and trace file is now under processing for SATT GUI"
                    print "Check http://" + sat_gui_server + " for progress"
                else:
                    print "Warning: SATT trace package '" + sat_tgz_file + "' not found!"
        else:
            self.url = 'http://localhost:5000/'
            python_path = 'python'
            if self.satt_venv_bin:
                python_path = os.path.join(self.satt_venv_bin, 'python')

            if self._args.PATH:
                if os.path.isdir(self._args.PATH):
                    # import data into local db
                    subprocess.call(python_path + ' ' +
                                    os.path.join(self.sat_home, 'satt', 'visualize', 'backend', 'db_import.py') +
                                    ' ' + self._args.PATH, shell=True)
                else:
                    print "ERROR: SATT trace '" + self._args.PATH + "' not found!"
                    sys.exit(-1)

            # start backend
            backend_call_string = python_path + ' ' + os.path.join(self.sat_home, 'satt', 'visualize', 'backend', 'sat-backend.py')
            print "launch backened"
            backend_handle = subprocess.Popen(backend_call_string.split(), preexec_fn=os.setsid)
            print "backened running"

        if not self._args.importonly:
            # launch browser with SAT UI
            print "start browser"
            webbrowser.open(self.url)
            print "check backend_handle"
            if backend_handle is not None:
                signal.signal(signal.SIGINT, signal_handler)
                print "****************************************"
                print "Press any key to stop satt visualizer..."
                print "****************************************\n\n"
                raw_input()
                os.killpg(backend_handle.pid, signal.SIGTERM)
