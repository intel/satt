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
import time
import argparse
import subprocess
# from satt.common import envstore
# from satt.common.control import AdbControl
# from satt.common.control import SshControl
# from satt.common.control import ShellControl


def main():
    adb = None

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
    parser.add_argument('HAYSTACKS', nargs='+', help='search path')
    args = parser.parse_args()

    # Take last haystack for file cache
    file_cache = args.HAYSTACKS[-1]

    if os.path.exists(args.NEEDLE):
        print args.NEEDLE.rstrip()
        return

    # Use first sat-path-map tool to search host side haystacks
    if args.path_mapper:
        sat_path_map_cmd = args.path_mapper + ' "' + args.NEEDLE + '" -k ' + args.kernel + ' -m ' + args.modules
        for hs in args.HAYSTACKS:
            sat_path_map_cmd += ' ' + hs
            response = subprocess.check_output(sat_path_map_cmd, shell=True)
    else:
        response = ''

    if response:
        print response.rstrip()
        return
    elif (adb is not None):
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
