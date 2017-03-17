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
import pickle

store = None


def get_instance():
    global store
    if store is None:
        store = EnvStore()
    return store


class EnvStore:
    _variables = {}
    _configs = []
    _sat_home = ''
    _conf_path = ''
    _sat_version = ''
    _sat_venv_bin = False

    def __init__(self):
        self._variables = {'sat_os': -1,
                           'sat_target_source': '',
                           'sat_target_build': '',
                           'sat_trace_logging_method': '',
                           'sat_control_bus': '',
                           'sat_control_ip': '',
                           'sat_login_id': '',
                           'sat_path_kernel': '',
                           'sat_path_kernel_src': '',
                           'sat_path_modules': '',
                           'sat_build_official': False}

        self._configs.append(self._variables)  # conf[0]: current setup
        self._configs.append(self._variables.copy())  # conf[1]: empty setup

    def load(self):
        self._conf_path = os.path.join(self._sat_home, 'conf', 'config.env')
        if os.path.exists(self._conf_path):
            self._configs = pickle.load(open(self._conf_path, 'rb'))
            self._variables = self._configs[0]

    def _set_default_values(self, variables):
        if 'sat_trace_logging_method' not in self._variables.keys():
            variables['sat_trace_logging_method'] = ''
        if 'sat_target_source' not in variables.keys():
            variables['sat_target_source'] = ''
        if 'sat_target_build' not in variables.keys():
            variables['sat_target_build'] = ''
        if 'sat_control_bus' not in variables.keys():
            variables['sat_control_bus'] = ''
        if 'sat_login_id' not in variables.keys():
            variables['sat_login_id'] = ''
        return variables

    def store(self):
        if self._conf_path != '':
            pickle.dump(self._configs, open(self._conf_path, 'wb'), pickle.HIGHEST_PROTOCOL)
        else:
            print ("WARNNING: SAT envsetup conf file path not set")

    def add_config(self, conf):
        for key in conf.keys():
            self._variables[key] = conf[key]
        # Check whether the same config already exists
        identical = False
        for idx, cnf in enumerate(self._configs):
            # skip the first config as it is the current one
            if idx == 0:
                continue
            if cmp(conf, cnf) == 0:
                identical = True
                break
        if identical is False:
            self._configs.append(self._variables.copy())
        else:
            print ("Identical configuration already stored, using existing one")
        pickle.dump(self._configs, open(self._conf_path, 'wb'), pickle.HIGHEST_PROTOCOL)

    def edit_config(self, idx, conf):
        if idx > 1 and idx < len(self._configs):
            for key in conf.keys():
                self._configs[idx][key] = self._variables[key] = conf[key]
            pickle.dump(self._configs, open(self._conf_path, 'wb'), pickle.HIGHEST_PROTOCOL)
            return True
        else:
            return False

    def remove_config(self, idx):
        if idx > 1 and (idx < len(self._configs)):
            self._configs.remove(conf=self._configs[idx])
            pickle.dump(self._configs, open(self._conf_path, 'wb'), pickle.HIGHEST_PROTOCOL)

    def get_config_list(self):
        return self._configs[:]

    def get_config(self, idx):
        if idx < (len(self._configs)):
            for key in self._configs[idx].keys():
                self._variables[key] = self._configs[idx][key]
            pickle.dump(self._configs, open(self._conf_path, 'wb'), pickle.HIGHEST_PROTOCOL)
        return self._set_default_values(self._variables.copy())

    def get_current(self):
        return self._set_default_values(self._variables.copy())

    def set_variable(self, key, value):
        if key in self._variables.keys():
            self._variables[key] = value
            return True
        else:
            return False

    def get_variable(self, key):
        if key in self._variables.keys():
            return self._variables[key]
        else:
            return None

    def get_sat_home(self):
        return self._sat_home

    def set_sat_home(self, sat_home):
        self._sat_home = sat_home
        verfile = os.path.join(self._sat_home, '.version')
        if os.path.isfile(verfile):
            self._sat_version = open(verfile).readline().rstrip()

    # Virtualenv binaries path
    def get_sat_venv_bin(self):
        return self._sat_venv_bin

    def set_sat_venv_bin(self, sat_venv_bin):
        self._sat_venv_bin = sat_venv_bin

    def get_sat_version(self):
        return self._sat_version

    def set_sat_version(self, version):
        self._sat_version = version.rstrip()
        open(os.path.join(self._sat_home, '.version'), 'w').write(version + '\n')
