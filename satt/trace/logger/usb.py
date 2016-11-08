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

""" Logger Class

"""

from logger import Logger
from satt.common import envstore
class UsbLogger(Logger):
    """ USB logger
    """
    def __init__(self, control):
        Logger.__init__(control)
        self._sat_home_path = envstore.store.get_sat_home()
        print "init"
    def initialize(self):
        print "initialize"
    def start_tracing(self):
        print "start_tracing"
    def stop_tracing(self):
        print "stop_tracing"
    def get_sideband_data(self):
        print "get_sideband_data"
    def get_trace_data(self):
        print "get_trace_data"
