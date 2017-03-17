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

###########################################################
# description: Configure satt environment for target device
###########################################################

import os
import sys
import glob
import platform
from satt.common import helper
from satt.common import envstore
from satt.common.targetos import targetos

##################################
# Command file for satt Config
##################################
class_name = "SattConfig"


class SattConfig:

    _config = None
    _variables = {}
    _readchar = None
    _os = None
    _helper = None

    # ===============================================#
    def __init__(self):
        self._config = envstore.store
        self._variables = self._config.get_current()
        if 'sat_trace_logging_method' not in self._variables.keys():
            self._variables['sat_trace_logging_method'] = ''
        if 'sat_target_source' not in self._variables.keys():
            self._variables['sat_target_source'] = ''
        if 'sat_target_build' not in self._variables.keys():
            self._variables['sat_target_build'] = ''
        if 'sat_control_bus' not in self._variables.keys():
            self._variables['sat_control_bus'] = ''
        self._helper = helper.get_instance()
        self._readchar = self._helper.get_readchar_object()

    # ===============================================#
    def action(self):
        os.system('cls' if os.name == 'nt' else 'clear')
        print '**************************************'
        print '*  Setup SATT environment variables! *'
        print '*  (*) [Enter]-key to choose default *'
        print '**************************************\n'
        print helper.color.BOLD + 'Current configuration:' + helper.color.END
        configurations_exits = True
        if self._variables['sat_os'] >= 0:
            print ('   ' + helper.color.BOLD + targetos.OsHelper.osdata[self._variables['sat_os']].Name + helper.color.END)
            print ('   ' + '-' * len(targetos.OsHelper.osdata[self._variables['sat_os']].Name))
            print ('   Trace out     : ' + helper.color.BOLD + self._variables['sat_trace_logging_method'] + helper.color.END)
            print ('   Control bus   : ' + helper.color.BOLD + self._variables['sat_control_bus'] + helper.color.END),
            if self._variables['sat_control_bus'] == 'SSH':
                print '-> ' + helper.color.BOLD + self._variables['sat_control_ip'] + helper.color.END
            else:
                print
            if self._variables['sat_target_source'] != "":
                print ('   Build path    : ' + helper.color.BOLD + self._variables['sat_target_source'] + helper.color.END)
            if os.path.basename(self._variables['sat_target_build']) != "":
                print ('   Product       : ' + helper.color.BOLD + os.path.basename(self._variables['sat_target_build']) +
                       helper.color.END)
            if self._variables['sat_path_kernel'] != "":
                print ('   Kernel path   : ' + helper.color.BOLD + self._variables['sat_path_kernel'] + helper.color.END)

            print
            print helper.color.BOLD + 'Change values:' + helper.color.END
            print ' * 0) No  - keep current setup'
            print '   1) Yes - select setup'
            selection = self._readchar()

        else:
            print "   None"
            selection = '1'
            configurations_exits = False

        print

        if selection == '1':
            if configurations_exits:
                self.select_prev_setup()
            self.set_os()
            self.set_control_bus()
            self.set_trace_destination()
            self._os = targetos.get_instance()
            self._os.config(self._variables)

            self._config.add_config(self._variables)
    # ===============================================#
    def set_os(self):
        print helper.color.BOLD + 'Select target OS:' + helper.color.END
        for osid in targetos.OsHelper.osdata.keys():
            if osid == self._variables['sat_os']:
                print ' *',
            else:
                print '  ',
            print str(osid) + ') ' + targetos.OsHelper.osdata[osid].Name
        selection = self._readchar()
        # default: set first item
        if selection.isdigit():
            if int(selection) in targetos.OsHelper.osdata.keys():
                value = int(selection)
                self._variables['sat_os'] = value
                self._config.set_variable('sat_os', value)

        print ('   Selected OS = ' + helper.color.BOLD + targetos.OsHelper.osdata[self._variables['sat_os']].Name +
               helper.color.END + '\n')

    # ===============================================#
    def set_trace_destination(self):
        self._variables['sat_trace_logging_method'] = 'RAM'
        print ('Autoselect trace destination: ' + helper.color.BOLD +
               self._variables['sat_trace_logging_method'] + helper.color.END + '\n')
        # methods = ['RAM', 'USB3']
        # h = helper.get_instance()
        # if h.pti_available():
        #    methods.append('PTI')
        # print helper.color.BOLD + 'Select trace destination:' + helper.color.END
        # for idx, val in enumerate(methods):
        #     if val == self._variables['sat_trace_logging_method']:
        #         print ' *',
        #     else:
        #         print '  ',
        #     print str(idx) + ') ' + val
        # value = ''
        # while True:
        #     value = self._readchar()
        #     if value.isdigit():
        #         if int(value) >= 0 and int(value) <= len(methods):
        #             self._variables['sat_trace_logging_method'] = methods[int(value)]
        #             break
        #     elif ord(value) == 13:
        #         break
        # print '   Trace destination = ' + self._variables['sat_trace_logging_method'] + '\n'

    # ===============================================#
    def set_control_bus(self):
        # methods = ['ADB', 'SSH', 'SHELL']
        methods = targetos.OsHelper.osdata[self._variables['sat_os']].ConnMethods
        if len(methods) == 1:
            print 'Autoselect control bus: ' + helper.color.BOLD + methods[0] + helper.color.END + '\n'
            self._variables['sat_control_bus'] = methods[0]
            self._config.set_variable('sat_control_bus', methods[0])
        else:
            print helper.color.BOLD + 'Select control bus:' + helper.color.END
            index = 0
            for idx, val in enumerate(methods):
                if val == self._variables['sat_control_bus']:
                    index = idx
                    print ' *',
                else:
                    print '  ',
                print str(idx) + ') ' + val
            while True:
                value = self._readchar()
                if value.isdigit():
                    if int(value) >= 0 and int(value) <= len(methods):
                        index = int(value)
                        break
                elif value == '' or ord(value) == 13:
                    break
            self._variables['sat_control_bus'] = methods[index]
            self._config.set_variable('sat_control_bus', methods[index])
            print '   Control bus = ' + self._variables['sat_control_bus'] + '\n'

        if self._variables['sat_control_bus'] == 'SSH':
            self.set_control_ip()
        else:
            self._variables['sat_control_ip'] = ''

    # ===============================================#
    def set_control_ip(self):
        # '--ip', action='store', help='IP Address to connect, e.g. --ip 192.168.1.10'
        print helper.color.BOLD + 'Set SSH IP address:' + helper.color.END
        correct = False
        while correct is False:
            correct = True
            if self._variables['sat_control_ip'] == '':
                self._variables['sat_control_ip'] = '127.0.0.1'
            ip = raw_input('   Give IP address to connect (current %s): ' %(self._variables['sat_control_ip'], ))
            if ip == '':
                break
            values = ip.split('.')
            if len(values) == 4:
                for d in values:
                    if not d.isdigit() or int(d) < 0 or int(d) > 255:
                        correct = False
                        break
            else:
                correct = False
            if correct is True:
                self._variables['sat_control_ip'] = ip
                print '   IP address = ' + ip
            else:
                print "Not a valid IP address format, try again"
        print

    # ===============================================#
    def select_prev_setup(self):
        setups = self._config.get_config_list()
        print helper.color.BOLD + 'Previous setups:' + helper.color.END
        max_key = max(len(targetos.OsHelper.osdata[x].Name) for x in targetos.OsHelper.osdata)
        last_setup = len(setups) - 1
        for idx, setup in enumerate(setups):
            if idx > 0:
                if idx == 1:
                    print '   ' + str(idx) + ') New setup'
                else:
                    star = ' '
                    if last_setup == idx:
                        star = '*'
                    print (' ' + star + ' ' + str(idx) + ') ' + helper.color.BOLD +
                           targetos.OsHelper.osdata[setup['sat_os']].Name.ljust(max_key) + helper.color.END),
                    print (': ' + helper.color.BOLD + setup['sat_trace_logging_method'] + helper.color.END +
                           '/' + helper.color.BOLD + setup['sat_control_bus'] + helper.color.END),
                    if setup['sat_control_ip'] != '':
                        print '-> ' + helper.color.BOLD + setup['sat_control_ip'] + helper.color.END
                    else:
                        print
                    if setup['sat_target_source'] != "":
                        print ('        path: ' + helper.color.BOLD + setup['sat_target_source'] + helper.color.END)
                    if os.path.basename(setup['sat_target_build']) != "":
                        print ('        prod: ' + helper.color.BOLD + os.path.basename(setup['sat_target_build']) +
                               helper.color.END)
                    if self._variables['sat_path_kernel'] != "":
                        print ('        kern: ' + helper.color.BOLD + setup['sat_path_kernel'] + helper.color.END)

        while True:
            value = raw_input('   :')
            if value.isdigit():
                if int(value) >= 0 and int(value) <= len(setups):
                    self._variables = self._config.get_config(int(value))
                    break
            elif value == '':
                break

    # ===============================================#
    def debug_print(self, idx):
        l = self._config.get_config_list()
        print ">>>" + str(idx)
        for i, setup in enumerate(l):
            print str(i) + ') ' + setup['sat_target_source']
        print "<<<" + str(idx)
