#!/usr/bin/env python
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
# description: Post-process taken satt trace
############################################

import os
import re
import sys
import glob
import shutil
import argparse
import subprocess
import fnmatch
from satt.common import envstore
from satt.process.linkmodules import LinkModules
from satt.process.binary_patch import BinaryPatch
from satt.common.targetos import targetos

##################################
# Command file for satt Process
##################################
class_name = "SattProcess"

IGNORE_SAT_MODULE_ON_PROCESSING = False
NO_PATCHING_IF_ALREADY_PATCHED = False


def intermediate_work(params):
    import subprocess
    import os
    commands = params[0]
    rm_files = params[1]
    for c in commands:
        subprocess.call(c, shell=True)
    for f in rm_files:
        os.remove(f)


class SattProcess:
    _bin_path = ''
    _trace_folder_path = ''
    _post_process_bin_path = ''
    _official_build = False
    _args = None
    _sat_home = ''
    _variables = {}
    _os = None

    # ===============================================#
    def __init__(self):
        self._sat_home = envstore.store.get_sat_home()
        self._satt_venv_bin = envstore.store.get_sat_venv_bin()
        self._post_process_bin_path = os.path.join(self._sat_home, 'satt', 'process', 'bin')
        self._variables = envstore.store.get_current()
        self._os = targetos.get_instance()
        self.ParseArguments()

    # ===============================================#
    def action(self):
        if not os.path.exists(self._os._trace_path):
            print "ERROR: SATT trace '" + self._os._trace_path + "' not found!"
            return

        if self._args.rtit:
            print ("*************************")
            print ("* Processing RTIT trace *")
            print ("*************************")
        else:
            print ("************************")
            print ("* Processing IPT trace *")
            print ("************************")

        self.RemoveHostFileCache()
        self.CopyBinariesToTraceFolder()
        self.PatchKernelBinaries()
        if self._os.is_os('Linux') or self._os.is_os('Yocto'):
            self.AdjustKernelVma()
        self.DecodeRawPtiData()


        retval = 0

        debug = False
        if self._args.debug_level:
            debug = True
        self.generate_model(debug, self._args.debug_level)
        if retval == 0:
            self.DemangleSymbols()
            # Adding SatVersion into satstats requires changes into sat ui backend
            # self.SatVersionIntoSatstats()
            subprocess.call(os.path.join(self._post_process_bin_path, 'post') + ' ' + self._os._trace_path + " | tee -a " +
                            os.path.join(self._os._trace_path, self._os._trace_path + '-process.log'), shell=True)
            subprocess.call(os.path.join(self._post_process_bin_path, 'pack') + ' ' + self._os._trace_path + " | tee -a " +
                            os.path.join(self._os._trace_path, self._os._trace_path + '-process.log'), shell=True)
        else:
            print "**************************************"
            print "**    Uups, Processing Failed       **"
            print "**    - please, try to trace again  **"
            print "**************************************"

    # ===============================================#
    def ParseArguments(self):
        parser = argparse.ArgumentParser(description='satt process')
        parser.add_argument('-d', '--debug', action='store', dest="debug_level", type=int,
                            help='Enable parser debug output. Level: 0 .. x', required=False)
        parser.add_argument('-i', '--ipt', action='store_true',
                            help='Process IPT traces', required=False)
        parser.add_argument('-r', '--rtit', action='store_true',
                            help='Process RTIT traces', required=False)
        parser.add_argument('-p', '--patching_disable', action='store_true',
                            help='Do not patch modules in case already patched',
                            required=False)
        parser.add_argument('TRACE_PATH', action='store', help='trace path')
        self._args = parser.parse_args()
        self._bin_path = os.path.join(self._sat_home, 'lib', 'post-process')

        self._os._trace_path = self._args.TRACE_PATH
        if self._os._trace_path[-1:] == "/" or self._os._trace_path[-1:] == "\\":
            self._os._trace_path = self._os._trace_path[:-1]

        # If called with absolute path
        if os.path.isabs(self._os._trace_path):
            savedPath = os.getcwd()
            self._trace_folder_path = os.path.realpath(os.path.join(self._os._trace_path, '..'))
            self._os._trace_path = os.path.basename(os.path.normpath(self._os._trace_path))
            os.chdir(self._trace_folder_path)

    # ===============================================#
    def RemoveHostFileCache(self):
        if os.path.exists(os.path.join(self._os._trace_path, 'binaries', 'sat-path-cache', 'cache')):
            os.remove(os.path.join(self._os._trace_path, 'binaries', 'sat-path-cache', 'cache'))

    # ===============================================#

    def CopyBinariesToTraceFolder(self):
        kernel_path = envstore.store.get_variable('sat_path_kernel')
        if not os.path.exists(os.path.join(self._os._trace_path, 'binaries', 'kernel')):
            os.makedirs(os.path.join(self._os._trace_path, 'binaries', 'kernel'))

        # 32-bit
        if os.path.isfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso32-sysenter.so.dbg')):
            shutil.copyfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso32-sysenter.so.dbg'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso32-sysenter.so'))
        elif os.path.isfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso32-sysenter.so')):
            shutil.copyfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso32-sysenter.so'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso32-sysenter.so'))
        elif os.path.isfile(os.path.join(os.path.dirname(kernel_path), 'vdso', 'vdso32.so')):
            shutil.copyfile(os.path.join(os.path.dirname(kernel_path), 'vdso', 'vdso32.so'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso32-sysenter.so'))

        # 64-bit
        if os.path.isfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso64.so.dbg')):
            shutil.copyfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso64.so.dbg'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso64.so'))
        elif os.path.isfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso64.so')):
            shutil.copyfile(os.path.join(kernel_path, 'arch', 'x86', 'vdso', 'vdso64.so'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso64.so'))
        elif os.path.isfile(os.path.join(os.path.dirname(kernel_path), 'vdso', 'vdso64.so')):
            shutil.copyfile(os.path.join(os.path.dirname(kernel_path), 'vdso', 'vdso64.so'),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vdso64.so'))

        if os.path.isfile(self._os.get_system_map_path()):
            shutil.copyfile(self._os.get_system_map_path(),
                            os.path.join(self._os._trace_path, 'binaries', 'kernel', 'System.map'))
        # copy original vmlinux to trace/binaries folder
        if os.path.isfile(self._os.get_vmlinux_path()):
            #
            extract_vmlinux = os.path.join(kernel_path, 'scripts', 'extract-vmlinux')
            if os.path.isfile(extract_vmlinux) and os.path.basename(self._os.get_vmlinux_path()) == 'vmlinuz':
                subprocess.call(extract_vmlinux + ' ' + self._os.get_vmlinux_path() + " > " +
                                os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vmlinux_'), shell=True)
            else:
                shutil.copyfile(self._os.get_vmlinux_path(),
                                os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vmlinux_'))
        # create modules dir
        if not os.path.exists(os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules')):
            os.makedirs(os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules'))
        # copy original kmods to trace/binaries folder
        modules_path = envstore.store.get_variable('sat_path_modules')
        if os.path.exists(modules_path):
            if self._os.is_os('Linux') or self._os.is_os('Yocto'):
                kmod_pattern = '*.ko'
                for root, dirs, files in os.walk(modules_path):
                    for filename in fnmatch.filter(files, kmod_pattern):
                        shutil.copyfile(os.path.join(root, filename), os.path.join(self._os._trace_path, 'binaries',
                                        'kernel', 'modules', os.path.basename(filename)+'_'))
            else:
                for f in glob.glob(os.path.join(modules_path, '*.ko')):
                    shutil.copyfile(f, os.path.join(self._os._trace_path, 'binaries',
                                    'kernel', 'modules', os.path.basename(f)+'_'))
        # move&rename sat.ko from trace binaries folder to binaries/kernel/modules/sat.ko_
        if not self._official_build:
            if not os.path.exists(os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules', 'sat.ko_')):
                if os.path.exists(os.path.join(self._os._trace_path, 'binaries', 'sat.ko')):
                    shutil.move(os.path.join(self._os._trace_path, 'binaries', 'sat.ko'),
                                os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules'))
                os.rename(os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules', 'sat.ko'),
                          os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules', 'sat.ko_'))

        self._os.copy_binaries()

    # ===============================================#
    def PatchKernelBinaries(self):
        if not self._args.patching_disable:
            bp = BinaryPatch(self._variables['sat_target_build'], os.path.realpath(self._os._trace_path))
            retstr = bp.patchModules(self._official_build, NO_PATCHING_IF_ALREADY_PATCHED,
                                     IGNORE_SAT_MODULE_ON_PROCESSING)
            if retstr == "no_dump":
                # dump files not found, perform linking
                print "No dump files found, perform linking for modules"
                lm = LinkModules(self._variables['sat_target_build'], os.path.realpath(self._os._trace_path))
                lm.linkModules(self._official_build, IGNORE_SAT_MODULE_ON_PROCESSING)

    # ===============================================#
    def AdjustKernelVma(self):
        print "AdjustKernelVma"
        kernel_address = 0
        python_path = 'python'

        if self._satt_venv_bin:
            python_path = os.path.join(self._satt_venv_bin, python_path)

        sub_python = os.popen(python_path + ' ' + os.path.join(self._sat_home,
                              'satt', 'common', 'sbparser', 'sbdump.py') + ' -c < ' +
                              os.path.join(self._os._trace_path, "sideband.bin"))
        if sub_python:
            while True:
                line = sub_python.readline()
                if not line:
                    break
                match = re.search("codedump @ ([0-9a-fA-F]+),\s+(\d+): (\S+)", line)
                if match:
                    if match.group(3).find("vmlinux") >= 0:
                        kernel_address = "0x" + match.group(1)
                        break

        if kernel_address > 0:
            # Adjust vma for vmlinux file
            vmlinux_path = os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vmlinux')
            if os.path.exists(vmlinux_path):
                data = subprocess.check_output(['objdump', '-h', vmlinux_path])
                for line in data.splitlines():
                    match = re.search('\d+ \.text\s+(\S+)\s+(\S+)', line)
                    if match:
                        orig_address = match.group(2)
                        offset = int(kernel_address, 16) - int(orig_address, 16)
                        subprocess.call(['objcopy', '--adjust-vma', str(offset), vmlinux_path])

            # Adjust vma for System.map file
            systemmap_path = os.path.join(self._os._trace_path, 'binaries', 'kernel', 'System.map')
            if os.path.exists(systemmap_path):
                os.rename(systemmap_path, systemmap_path + '_')
                offset = 0;
                inf = open(systemmap_path+'_', 'r')
                outf = open(systemmap_path, 'w')
                while True:
                    line = inf.readline()
                    if not line:
                        break
                    match = re.search('(\S+)\s+(\S)\s+(\S+)', line)
                    if match:
                        if match.group(2) == 'T' and match.group(3) == '_text':
                            offset = int(kernel_address, 16) - int(match.group(1), 16)
                        addr = format(int(match.group(1), 16) + offset, 'x')
                        addr = addr.zfill(len(match.group(1)))
                        line = addr + ' ' + match.group(2) + ' ' + match.group(3) + '\n'
                    outf.write(line)

    # ===============================================#
    def DecodeRawPtiData(self):
        if not os.path.isfile(os.path.join(self._os._trace_path, 'cpu0.bin')):
            if os.path.isfile(os.path.join(self._os._trace_path, 'stma.raw')):
                print 'Decode cpu rtit streams from PTI stream'
                savedPath_ = os.getcwd()
                os.chdir(self._os._trace_path)
                size_ = os.path.getsize('stma.raw')
                subprocess.call(os.path.join(self._post_process_bin_path, 'sat-stp-dump') + ' -s ' + str(size_) +
                                ' -m < stma.raw', shell=True)
                os.chdir(savedPath_)

    # ===============================================#
    def generate_model(self, debug, debug_level):
        # Make collection
        import multiprocessing
        max_procs = multiprocessing.cpu_count()
        command = ''
        collection_file = os.path.join(self._os._trace_path, self._os._trace_path + '.collection')

        # commands for rtit/ipt
        collection_make_version = ''
        collection_model_version = ''
        sat_collection_cbr_version = ''
        sat_collection_stats_version = ''
        sat_collection_tasks_version = ''
        if self._args.rtit:
            collection_make_version = 'sat-rtit-collection-make'
            collection_model_version = 'sat-rtit-collection-model'
            sat_collection_cbr_version = 'sat-rtit-collection-cbr'
            sat_collection_stats_version = 'sat-rtit-collection-stats'
            sat_collection_tasks_version = 'sat-rtit-collection-tasks'
        else:
            collection_make_version = 'sat-ipt-collection-make'
            collection_model_version = 'sat-ipt-model'
            sat_collection_cbr_version = 'sat-ipt-collection-cbr'
            sat_collection_stats_version = 'sat-ipt-collection-stats'
            sat_collection_tasks_version = 'sat-ipt-collection-tasks'

        print 'MAKING COLLECTION ' + collection_file

        command = (os.path.join(self._post_process_bin_path, collection_make_version) +
                   ' -s ' + os.path.join(self._os._trace_path, 'sideband.bin'))
        cpu_files = glob.glob(os.path.join(self._os._trace_path, 'cpu*.bin'))
        cpu_files.sort()
        for i in cpu_files:
            command += ' -r ' + i
        command += ' | grep -v "^#" > ' + collection_file
        #print "COMMAND=" + command

        # Execute: MAKE COLLECTION

        ret = subprocess.call(command, shell=True)


        # Building execution model
        print 'BUILDING EXECUTION MODEL ON COLLECTION ' + collection_file

        python_path = 'python'

        if self._satt_venv_bin:
            python_path = os.path.join(self._satt_venv_bin, python_path)

        path_helper = (python_path + " " +
                       os.path.join(self._sat_home, 'satt', 'process', 'binary_server.py') + " '%s' " +
                       "-p " + os.path.join(self._post_process_bin_path, 'sat-path-map') + " " +
                       "-k " + os.path.join(self._os._trace_path, 'binaries', 'kernel', 'vmlinux') + " " +
                       "-m " + os.path.join(self._os._trace_path, 'binaries', 'kernel', 'modules') + " ")

        path_helper += "-d " + self._os.get_debug_paths() + " "

        path_helper += (os.path.join(self._os._trace_path, 'binaries', 'symbols') + " " +
                       os.path.join(self._os._trace_path, 'binaries') + " " +
                       os.path.join(self._os._trace_path, 'binaries', 'sat-path-cache'))

        command = (os.path.join(self._post_process_bin_path, collection_model_version) +
                   ' -C ' + collection_file +
                   ' -m ' + os.path.join(self._os._trace_path, 'binaries', 'kernel', 'System.map') +
                   ' -f "' + path_helper + '"'
                   ' -F ' + os.path.join(self._os._trace_path, 'binaries', 'sat-path-cache') +
                   ' -P ' + str(max_procs) +
                   ' -o ' + os.path.join(self._os._trace_path, self._os._trace_path + '-%u.model') +
                   ' -w ' + os.path.join(self._os._trace_path, self._os._trace_path + '-%u.lwm') +
                   ' -n ' + os.path.join(self._os._trace_path, self._os._trace_path + '.satsym') +
                   ' -e ' + os.path.join(self._os._trace_path, self._os._trace_path + '.satmod') +
                   ' -h ' + os.path.join(self._os._trace_path, self._os._trace_path + '.satmodh'))
        if debug:
            command += ' -d'
            command += ' -D' * debug_level

        # Execute: BUILD MODELS
        ret = subprocess.call(command, shell=True)

        print "INTERMEDIATE PROCESSING",
        print "AND SHRINKING MODEL" if not debug else ''

        tid_files = glob.glob(os.path.join(self._os._trace_path, self._os._trace_path + '-*.model'))
        tid_files.sort()
        for i, t in enumerate(tid_files):
            tid_files[i] = os.path.splitext(os.path.basename(t))[0]
        tid_string = '\n'.join(tid_files)
        command = ('echo "' + tid_string + '" | xargs -n 1 --max-procs=' + str(max_procs) + ' -I PER_TID bash -c ' +
                   '"' + os.path.join(self._post_process_bin_path, 'sat-intermediate'))
        if debug:
            command += ' -d '
        command += (' -w ' + os.path.join(self._os._trace_path, 'PER_TID.lwm') +
                    ' -o ' + os.path.join(self._os._trace_path, 'PER_TID.sat') +
                    ' ' + os.path.join(self._os._trace_path, 'PER_TID.model') + ';')
        if not debug:
            command += (' rm ' + os.path.join(self._os._trace_path, 'PER_TID.model') +
                        ' ' + os.path.join(self._os._trace_path, 'PER_TID.lwm') + ';')
            command += (' ' + os.path.join(self._post_process_bin_path, 'sat-shrink-output') +
                        ' ' + os.path.join(self._os._trace_path, 'PER_TID.sat'))
        command += ('" | ' + os.path.join(self._post_process_bin_path, 'sat-post') +
                    ' -o ' + os.path.join(self._os._trace_path, self._os._trace_path + '.log'))

        # Execute: INTERMEDIATE
        subprocess.call(command, shell=True)

        if not debug:
            print "MERGING MODEL"
            command = (os.path.join(self._post_process_bin_path, 'sat-merge') +
                       ' ' + os.path.join(self._os._trace_path, self._os._trace_path + '-*.sat') +
                       ' > ' + os.path.join(self._os._trace_path, self._os._trace_path + '.sat0'))
            subprocess.call(command, shell=True)

            # Generate satcbr
            command = (os.path.join(self._post_process_bin_path, sat_collection_cbr_version) +
                       ' ' + collection_file + ' | grep -v "^#" > ' +
                       os.path.join(self._os._trace_path, self._os._trace_path + '.satcbr'))
            subprocess.call(command, shell=True)

            # Generate satstats
            command = (os.path.join(self._post_process_bin_path, sat_collection_stats_version) +
                       ' ' + collection_file + ' | grep -v "^#" > ' +
                       os.path.join(self._os._trace_path, self._os._trace_path + '.satstats'))
            subprocess.call(command, shell=True)

            # Generate satp
            command = (os.path.join(self._post_process_bin_path, sat_collection_tasks_version) +
                       ' ' + collection_file + ' | grep -v "^#" > ' +
                       os.path.join(self._os._trace_path, self._os._trace_path + '.satp'))
            subprocess.call(command, shell=True)

            print "REMOVING PER-PROCESS MODELS"
            sat_files = glob.glob(os.path.join(self._os._trace_path, self._os._trace_path + '-*.sat'))
            for f in sat_files:
                os.remove(f)

    # ===============================================#
    def DemangleSymbols(self):
        # demangle symbol names in satsyms file
        print "demangle symbols.."
        operators = ['<<=', '>>=', '->*', '<<', '>>', '<=', '>=', '->', '>', '<']
        satsym_file = self._os._trace_path + '/' + self._os._trace_path + '.satsym'
        tmpfile = self._os._trace_path + '/' + self._os._trace_path + '.satsym_'
        if os.path.isfile(satsym_file):
            os.rename(satsym_file, tmpfile)
            fin = open(tmpfile, 'r')
            fout = open(satsym_file, 'w')
            while True:
                output = ''
                line = fin.readline()
                if not line:
                    break
                demagle_success = False
                sid, sym = line.rstrip().split(';')
                plt = ''
                pre_dl = ''
                if sym.startswith('__dl_'):
                    sym = sym[5:]
                    pre_dl = '__dl_'
                if sym.startswith('_Z'):
                    if sym.endswith('@plt'):
                        sym = sym[:-4]
                        plt = '@plt'
                    dec = subprocess.check_output('c++filt -p ' + sym, shell=True).rstrip()
                    # rip off possible template definitions to reduce symbol size
                    if dec != '' and dec[:2] != '_Z':
                        demagle_success = True

                        # exclude operator having '<' or '>'
                        blocked_area = [-9, -9]
                        operidx = dec.find("::operator")
                        if operidx >= 0:
                            for op in operators:
                                if dec[operidx+10:(operidx+10 + len(op))] == op:
                                    blocked_area[0] = operidx
                                    blocked_area[1] = operidx+10+len(op)
                        idx = 0
                        while True:
                            idx = dec.find('>', idx)
                            if idx < 0:
                                break
                            if idx >= blocked_area[0] and idx <= blocked_area[1]:
                                idx += 1
                                continue
                            idx += 1
                            ridx = dec.rfind('<', 0, idx)
                            while ridx >= blocked_area[0] and ridx <= blocked_area[1]:
                                ridx = dec.rfind('<', 0, ridx)
                            if ridx < 0:
                                demagle_success = False
                                print "Warning: template parenthesis does not match!!! (" + str(sid) + ")"
                                break
                            dec = dec[:ridx] + ';' * (idx - ridx) + dec[idx:]
                        if demagle_success:
                            output = sid + ';' + pre_dl + dec.replace(';', '') + plt + ';' + sym + '\n'

                if not demagle_success:
                    output = sid + ';' + sym + ';' + sym + '\n'
                fout.write(output)

            os.remove(tmpfile)

    # ===============================================#
    def SatVersionIntoSatstats(self):
        satstats_file = self._os._trace_path + '/' + self._os._trace_path + '.satstats'
        ver = envstore.store.get_sat_version()
        f = open(satstats_file, 'w+')
        f.write('VERSION|' + ver + '|SATT tool version used for post-processing')
