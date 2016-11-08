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
from subprocess import call
import uuid
#from status import Status
import status as stat
import shutil
import glob

#Expect normal folder structure!
# -sat
#      -backend
#      -bin
# Detect SAT HOME DIR
SAT_HOME = os.environ.get('SAT_HOME')
BIN_PATH = os.path.realpath(SAT_HOME + '/bin/')

status = stat.getStatus()

# 1.st step to process file is to unpack file
def process_trace(trace_id):

    trace_file = status.get_file_by_id(trace_id)
    status.update_status(trace_id, status.UNZIP)
    devnull = open('/dev/null', 'w')
    upload_traces_path = os.path.dirname(os.path.abspath(trace_file))
    tmp_process_path = upload_traces_path + "/" + str(uuid.uuid4())
    os.makedirs(tmp_process_path)

    ret_val = call("cd " + tmp_process_path + "; tar xvzf " + trace_file, shell=True, stdout=devnull)
    devnull.close()

    if not ret_val == 0:
        print "Unpack failed, bail out!"
        status.update_status(trace_id, status.FAILED, "unpacking failed")
        return

    print "Unpack done in {0}, status={1} ".format(tmp_process_path, ret_val)
    #TODO Update status to DB

    #Cleanup orginal file trace file
    os.remove(trace_file)

    unpacked_trace_folder = os.listdir(tmp_process_path)[0]

    # Check if incoming package has not been already processed, so try to process first
    if not _check_ready_to_import(tmp_process_path + "/" + unpacked_trace_folder):
        status.update_status(trace_id, status.PROCESS)
        # Process Trace
        ret = _process_trace(tmp_process_path + "/" + unpacked_trace_folder)
        if not (ret and _check_ready_to_import(tmp_process_path + "/" + unpacked_trace_folder)):
            status.update_status(trace_id, status.FAILED, "Processing failed")
            shutil.rmtree(trace_path)
            return

    status.update_status(trace_id, status.IMPORT)
    #Import Trace to DB
    ret = _import_trace(tmp_process_path + "/" + unpacked_trace_folder, trace_id)
    if not ret:
        status.update_status(trace_id, status.FAILED, "Importing trace to DB Failed")

    status.update_status(trace_id, status.READY)

def _check_ready_to_import(trace_path):
    if glob.glob(trace_path + "/*.sat0") and glob.glob(trace_path + "/*.satp") and \
        glob.glob(trace_path + "/*.satmod")  and glob.glob(trace_path + "/*.satsym"):
        return True
    return False

# 2.nd step to process file is to process trace
def _process_trace(trace_path):
    devnull = open('/dev/null', 'w')
    ret_val = call(BIN_PATH + "/sat-process " + trace_path, shell=True, stdout=devnull)
    if ret_val == 255:
        return False
    devnull.close()
    return True


# 3.rd step to process file is to import trace to db
def _import_trace(trace_path, trace_id):
    print "Import trace START" + str(trace_path)
    environment = os.environ.copy()
    #environment['SAT_HOME'] = SAT_HOME
    devnull = open('/dev/null', 'w')
    print BIN_PATH + "/sat-import " + trace_path
    ret_val = call(BIN_PATH + "/sat-import -i " + str(trace_id) + " " + trace_path, env=environment, shell=True, stdout=devnull)
    devnull.close()

    # Clear the space and remove tmp files
    shutil.rmtree(trace_path)

    if ret_val != 0:
        status.update_status(trace_id, status.FAILED, 'Importing trace to DB Failed')
        return False

    return True


# Receive already processes tarce in tgz format
def import_trace_to_server(trace_id):
    trace_file = status.get_file_by_id(trace_id)
    status.update_status(trace_id, status.UNZIP)
    try:
        devnull = open('/dev/null', 'w')
        trace_path = os.path.dirname(os.path.abspath(trace_file))
        ret_val = call("cd " + trace_path + "; tar xvzf " + trace_file, shell=True, stdout=devnull)
        if ret_val != 0:
            status.update_status(trace_id, status.FAILED, 'Untar Failed ' + trace_file)
            shutil.rmtree(trace_path)
            raise
        devnull.close()
    except:
        return False

    #Cleanup orginal file trace file
    os.remove(trace_file)
    unpacked_trace_folder = os.listdir(trace_path)[0]

    status.update_status(trace_id, status.IMPORT)
    if _import_trace(os.path.join(trace_path, unpacked_trace_folder), trace_id):
        status.update_status(trace_id, status.READY)
    else:
        status.update_status(trace_id, status.FAILED, 'Importing trace to DB Failed')

    return True
