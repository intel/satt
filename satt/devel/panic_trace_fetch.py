#/usr/bin/env python
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

import numpy as np
import os
import sys
import argparse

from pyadb import ADB
adb = ADB('adb')

PAGE_SIZE = 1024 * 1024 * 4
SIDEBAND_BUF_SIZE = PAGE_SIZE * 2

SAT_HOME = os.environ.get('SAT_HOME')

parser = argparse.ArgumentParser(description='sat-panic-fetch fetch crash data from system')
parser.add_argument('tracename', nargs='?', default=False, help='Panic trace matching trace filename')
parser.add_argument('-p', '--panic', action='store', help='Panic tracing mode: 1=Normal, 2=Hooked(default)',
                    required=False, default=2)
args = parser.parse_args()


def CrashlogKey(s):
    return int(s[8:])

print "... beginning"

print "set adb to rootmode"
print adb.set_adb_root(1)
adb.wait_for_device()
root = adb.shell_command("id")
#print root

path = os.getcwd()
name = args.tracename
if not (name and os.path.exists(path + '/' + name)):
    name = raw_input("Enter <<trace name>> to to fetch panic trace? :")
if name:
    # Save the Traces
    path = path + '/' + name

    if not os.path.exists(path):
        print "Pre-trace info was not found from " + path
        sys.exit()

    # create symbols dir for ART binary support
    p = os.popen("mkdir " + path + "/symbols")
    p.close()
    #p = os.popen("ln -s " + common_binary_path + "/symbols")
    #p.close()

    res = adb.shell_command("ls /storage/sdcard0/logs/ | grep crashlog")
    crashlogs = res.split()
    crashlogs = sorted(crashlogs, key=CrashlogKey)

    # Find Last Entry if exists
    if crashlogs[-1]:
        crashfiles = adb.shell_command("ls /storage/sdcard0/logs/" + crashlogs[-1]).split()
        gbuffer_console = False
        gbuffer_bin = False
        for crash in crashfiles:
            print crash
            if crash.startswith('emmc_ipanic_gbuffer'):
                gbuffer_bin = crash
            if crash.startswith('emmc_ipanic_console'):
                gbuffer_console = crash

        if not (gbuffer_console and gbuffer_bin):
            print "gbuffer files were not found from {0} folder".format("/storage/sdcard0/logs/" + crashlogs[-1])
            sys.exit()

        print "Fetch " + gbuffer_console + " from device"
        p = os.popen("adb pull " + "/storage/sdcard0/logs/" + crashlogs[-1] + "/" + gbuffer_console + " " + path)
        p.close()
        print "Fetch " + gbuffer_bin + " from device"
        p = os.popen("adb pull " + "/storage/sdcard0/logs/" + crashlogs[-1] + "/" + gbuffer_bin + " " + path)
        p.close()

        data = open(path + "/" + gbuffer_bin, "rb")
        gbuf_header_dtype = np.dtype([
            ('marker', '<i4'),
            ('number_of_cpus', '<i4'),
            ('sideband', '<i4'),
            ('ipt_ctl', '<u4'),
        ])
        header = np.fromfile(data, gbuf_header_dtype, 1)
        number_of_cpus = header['number_of_cpus'][0]
        sideband = header['sideband'][0]

        gbuffer_data_dtype = np.dtype([
            ('marker', '<i4'),
            ('number_of_cpus', '<i4'),
            ('sideband', '<i4'),
            ('ipt_ctl', '<u4'),
            ('buffer_infos', [
                ('cpu_id', '<i4'),
                ('offset', '<i4'),
                ('base_addr', '<u8'),
            ], number_of_cpus),
            ('ipts', [
                ('ipt', '<i1', PAGE_SIZE)
            ], number_of_cpus),
            ('sb', '<i1', sideband),
        ])
        data.seek(0)
        gbuffer = np.fromfile(data, gbuffer_data_dtype, 1)
        for x in range(0, number_of_cpus):
            ipt_path = path + "/cpu" + str(x) + ".bin"
            gbuffer['ipts'][0][x].tofile(ipt_path)
        if sideband > 0:
            sb_path = path + "/sideband.bin"
            gbuffer['sb'].tofile(sb_path)

    else:
        print "Crashlog not found"

else:
    print "Trace discarded"
