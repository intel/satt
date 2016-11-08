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
import glob
import pickle
import hashlib
import readline

helper = None


def get_instance():
    global helper
    if helper is None:
        helper = SatHelper()
    return helper


class color:
    PURPLE = '\033[95m'
    CYAN = '\033[96m'
    DARKCYAN = '\033[36m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    END = '\033[0m'


def complete(text, state):
    item = (glob.glob(text + '*') + [None])[state]
    if os.path.isdir(item):
        item += os.path.sep
    return item


class SatHelper:
    _trace_info = None
    _trace_folder = ''
    _readchar = None

    def __init__(self):
        pass

    def set_trace_folder(self, trace_folder):
        self._trace_folder = trace_folder
        self._trace_info = self.getTraceBuildInfo()

    # getTraceBuildInfo
    #  Load Trace Build info from the file
    #
    def getTraceBuildInfo(self):
        build_info = pickle.load(open(self._trace_folder + "/build_info.p", "rb"))
        return build_info

    # Calculate folder hash from trace's build info
    #
    def calculateTraceInfoHash(self):
        h = hashlib.md5()
        h.update(self._trace_info['name'])
        h.update(self._trace_info['device'])
        h.update(self._trace_info['version'])
        h.update(self._trace_info['type'])
        h.update(self._trace_info['user'])
        return h.hexdigest()

    # Object to handle one character input from user
    #
    def get_readchar_object(self):
        if self._readchar is None:
            try:
                mod = __import__('readchar')
                self._readchar = getattr((mod), 'readchar')
            except ImportError:
                self._readchar = raw_input
        return self._readchar

    def prepare_readline(self):
        readline.set_completer_delims(' \t\n;')
        readline.parse_and_bind("tab: complete")
        readline.set_completer(complete)

    def pti_available(self):
        from satt.common import envstore
        s = envstore.get_instance()
        return os.path.exists(os.path.join(s.get_sat_home(), "satt", "trace", "t32"))
