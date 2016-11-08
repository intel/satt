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
# Cron script to check if upload folder has new files
import glob
import os
import subprocess
from redis import Redis
from rq import Queue
import worker
import uuid
import status as stat

# Work Queues
queue = Queue(connection=Redis())

status = stat.getStatus()

BACKEND_DIR = os.path.dirname(os.path.abspath(__file__))
SAT_HOME = os.environ.get('SAT_HOME')
#SAT_HOME = os.path.realpath(BACKEND_DIR + '/../')
BIN_PATH = os.path.realpath(SAT_HOME + '/bin/')

#Server upload folder
SCP_UPLOAD_FOLDER = "/zdata/sat-chroot/upload/"
SAT_IMPORT_FOLDER = BACKEND_DIR + '/tmp/'

if not os.path.exists(SAT_IMPORT_FOLDER):
    os.makedirs(SAT_IMPORT_FOLDER)

for fn in glob.glob(SCP_UPLOAD_FOLDER + "*.tgz"):
    trace_tgz = os.path.abspath(fn)
    try:
        # Check if file is still open?
        subprocess.check_output("lsof -- " + trace_tgz, shell=True)
        print 'File still uploading ' + trace_tgz
    except:
        print 'File ready to import ' + trace_tgz
        try:
            trace_file_name = os.path.basename(trace_tgz)
            sat_trace_uniq = SAT_IMPORT_FOLDER + "/" + str(uuid.uuid4())
            # Create tmp dir and move file to import dir
            os.makedirs(sat_trace_uniq)
            sat_trace_uniq_path = os.path.join(sat_trace_uniq, trace_file_name)

            os.rename(trace_tgz, sat_trace_uniq_path)

            trace_id = status.create_id(sat_trace_uniq_path, trace_file_name)
            result = queue.enqueue(worker.import_trace_to_server, trace_id, timeout=7200)
        except:
            print "Err something whent wrong"
    pass
