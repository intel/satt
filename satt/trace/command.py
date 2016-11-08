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

##################################
# description: Take a trace
##################################

import os
import sys
import argparse
from satt.common import envstore
from satt.trace.logger import RamLogger
from satt.trace.logger import PtiLogger
from satt.trace.logger import UsbLogger
from satt.trace.logger import PanicLogger
from satt.common.control import control

##################################
# Command file for satt Trace
##################################
class_name = "SattTrace"


class SattTrace:
    DEBUG = False
    _variables = {}
    _control = None
    _logger = None

    def __init__(self):
        self._variables = envstore.store.get_current()

    def action(self):
        self._control = control.get_instance()

        # SAT_TRACE_LOGGING_METHOD
        if self._variables['sat_trace_logging_method'] == "PTI":
            print "PTI logger"
            self._logger = PtiLogger(self._control)
        elif self._variables['sat_trace_logging_method'] == "USB":
            print "USB3 logger"
            self._logger = UsbLogger(self._control)
        elif self._variables['sat_trace_logging_method'] == "PANIC":
            print "PANIC logger"
            self._logger = PanicLogger(self._control)
        else:  # self._variables['sat_trace_logging_method'] == "RAM":
            print "RAM logger"
            self._logger = RamLogger(self._control)

        print "Start tracing"

        self._logger.initialize()

        raw_input("Press Enter to Start SAT " + self._variables['sat_trace_logging_method'] + "-tracing...")
        self._logger.start_tracing()

        print "."
        raw_input("Press Enter to Stop Tracing...")
        self._logger.stop_tracing()

        trace_name, trace_path = self._logger.get_trace_name()
        if trace_path:
            print "Traces Will be stored into " + trace_path + " folder"
            self._logger.get_data(trace_path, trace_name)
        # TODO: Add envstore.store current config into trace folder for processing usage.
