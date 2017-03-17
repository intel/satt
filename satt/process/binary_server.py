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
import re
import sys
import time
import argparse
import subprocess
import pickle
# from satt.common import envstore
# from satt.common.control import AdbControl
# from satt.common.control import SshControl
# from satt.common.control import ShellControl

SATT_PATH = os.path.split(os.path.split(os.path.dirname(os.path.realpath(__file__)))[0])[0]
SATT_CONTROL_BUS = None
SATT_SSH_IP = None
SATT_OS = None

def load_config():
    global SATT_CONTROL_BUS
    global SATT_SSH_IP
    global SATT_OS
    conf_path = os.path.join(SATT_PATH, 'conf', 'config.env')
    if os.path.exists(conf_path):
        configs = pickle.load(open(conf_path, 'rb'))
        variables = configs[0]
        SATT_CONTROL_BUS = variables['sat_control_bus']
        SATT_SSH_IP = variables['sat_control_ip']
        SATT_OS = variables['sat_os']

def check_for_debug_symbols(filename):
    symbol_file = None
    ret_val = subprocess.check_output("nm -gC " + filename, stderr=subprocess.STDOUT, shell=True)
    line = ''
    if ret_val is not None and ret_val != "":
        line = ret_val.splitlines()[0]

    # Check if object contains symbols
    if "no symbols" in line:
        ret_val = subprocess.check_output("readelf -n " + filename, stderr=subprocess.STDOUT, shell=True)
        match = re.search("Build ID: (\w\w)(\w+)", ret_val)
        if match:
            debug_path_elems = filename.split(os.sep)
            # 1st trial to find stripped symbols
            if len(debug_path_elems) > 2:
                symbol_file = os.path.join(os.sep, debug_path_elems[1], debug_path_elems[2],
                                           'debug', '.build-id', match.group(1),
                                           match.group(2) + '.debug')
            # 2nd trial to find stripped symbols
            if not symbol_file or ( symbol_file and not os.path.exists(symbol_file)):
                fn_parts = os.path.split(filename)
                if len(fn_parts) == 2:
                    # Build ID will be matched by check_for_debug_symbols caller
                    symbol_file = os.path.join(fn_parts[0],'.debug',fn_parts[1])
    else:
        pass

    if symbol_file and os.path.exists(symbol_file):
        return True, symbol_file
    else:
        return False, None

def get_build_id(filename):
    ret_val = subprocess.check_output("readelf -n " + filename, stderr=subprocess.STDOUT, shell=True)
    match = re.search("Build ID: (\w+)", ret_val)
    if match:
        return match.group(1)
    return None

#Check that object and symbols build id match
def check_if_build_id_match(symbol, debug):
    symbol_bid = get_build_id(symbol)
    debug_bid = get_build_id(debug)
    return symbol_bid == debug_bid

def main():
    adb = None
    response = None
    debug = None

    load_config()

    if SATT_CONTROL_BUS == 'ADB':
        try:
            pyadb = __import__('pyadb')
            adb = pyadb.ADB('adb')
        except:
            adb = None

    parser = argparse.ArgumentParser(description='binary server')
    parser.add_argument('NEEDLE', help='file to find')
    parser.add_argument('-p', '--path_mapper', help='Path to sat-path-map binary', required=False)
    parser.add_argument('-k', '--kernel',  help='Path to kernel (vmlinux)', required=False)
    parser.add_argument('-m', '--modules', help='Path to kernel modules',   required=False)
    parser.add_argument('-d', '--debug', help='Debug symbols paths, ; separeted string',   required=False)
    parser.add_argument('HAYSTACKS', nargs='+', help='search path')
    args = parser.parse_args()

    # Take last haystack for file cache
    file_cache = args.HAYSTACKS[-1]

    if os.path.exists(args.NEEDLE):
        response = args.NEEDLE.rstrip()

    # Use first sat-path-map tool to search host side haystacks
    elif args.path_mapper:
        sat_path_map_cmd = args.path_mapper + ' "' + args.NEEDLE + '" -k ' + args.kernel + ' -m ' + args.modules
        for hs in args.HAYSTACKS:
            sat_path_map_cmd += ' ' + hs
        response = subprocess.check_output(sat_path_map_cmd, shell=True)
        if response:
            split = response.split(';')
            response = split[0].rstrip()
            if len(split) > 1:
                debug = split[0].rstrip()
    else:
        response = ''

    if response:
        # Search by build id (Ubuntu style)
        if SATT_OS == 0 or SATT_OS == 3: # Linux or Yocto
            status, debug = check_for_debug_symbols(response)

        # Check if debug symbols found by name in given paths
        if (SATT_OS == 3 or (SATT_OS == 0 and not debug)): #and args.debug:
            sat_path_map_cmd = args.path_mapper + ' "' + args.NEEDLE + '" -k ' + args.kernel + ' -m ' + args.modules
            for hs in args.debug.split(';'):
               sat_path_map_cmd += ' ' + hs
            debug = subprocess.check_output(sat_path_map_cmd, shell=True)

        if SATT_OS == 1 or SATT_OS == 2: # Chrome OS or Android
            debug = None

        # Check that build id matches with symbol and debug
        if debug and not check_if_build_id_match(response, debug):
            debug = None

        if debug:
            print response + ";" + debug.rstrip()
        else:
            print response + ";" + response
        return

    elif ( SATT_CONTROL_BUS == 'ADB' and adb is not None):
        # Try to get binary from target device
        if not adb.check_path():
            return
        root = adb.shell_command("id")
        if not root:
            return
        root = ''
        while (root is not None) and (root.find('uid=0(root)') == -1):
            if adb.set_adb_root.func_code.co_argcount == 1:
                root = adb.set_adb_root()
            else:
                root = adb.set_adb_root(1)
            time.sleep(2)
            root = adb.shell_command("id")
        if root is None:
            return
        else:
            # Fetch binary from target device
            host_file_name = os.path.join(file_cache, os.path.basename(args.NEEDLE))
            # control.get_remote_file(args.NEEDLE, host_file_name)
            adb.get_remote_file(args.NEEDLE, os.path.dirname(host_file_name))
            if os.path.isfile(host_file_name):
                print host_file_name.rstrip()
            return
    else:
        return


if __name__ == "__main__":
    main()
