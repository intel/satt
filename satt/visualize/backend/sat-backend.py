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
import psycopg2
import psycopg2.extras
import datetime
import simplejson as json
from flask import Flask, request, jsonify, send_file, abort
from werkzeug import secure_filename
import glob
from flask import g
from werkzeug.local import LocalProxy
if not sys.platform.startswith('win'):
    from redis import Redis
    from rq import Queue

SAT_HOME = os.environ.get('SAT_HOME')
# Set SAT_HOME for rest of the backend
if SAT_HOME is None:
    satHome = os.path.realpath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
    os.environ['SAT_HOME'] = satHome

import status as stat
import worker

if SAT_HOME:
    app = Flask(__name__, static_url_path='',
                static_folder=os.path.join(SAT_HOME, 'satt', 'visualize', 'webui'))
else:
    SAT_HOME = '.'
    app = Flask(__name__)

UPLOAD_FOLDER = os.path.join(SAT_HOME, 'satt', 'visualize', 'backend', 'tmp')
ALLOWED_EXTENSIONS = set(['tgz'])

status = stat.getStatus()

if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

app.debug = True
DEBUG = False

INS_MORE_LIMIT = 1000

# Global DB Definitions per request
db = None
cur = None
named_cur = None


def get_db():
    db = getattr(g, '_database', None)
    if db is None:
        g._database = psycopg2.connect(
            dbname=status.getDbConfig('dbname'),
            user=status.getDbConfig('user'),
            password=status.getDbConfig('password'))
        g._database.autocommit = True
        db = g._database
    return db


@app.teardown_appcontext
def teardown_db(exception):
    db = getattr(g, '_database', None)
    if db is not None:
        db.close()

dthandler = lambda obj: (
    obj.isoformat()
    if isinstance(obj, datetime.datetime) or isinstance(obj, datetime.date)
    else None)

# Work Queues
if not sys.platform.startswith('win'):
    queue = Queue(connection=Redis())


@app.before_request
def before_request():
    global db, cur, named_cur
    db = LocalProxy(get_db)
    cur = db.cursor()
    named_cur = db.cursor(cursor_factory=psycopg2.extras.NamedTupleCursor)


@app.route('/api/1/screenshot/<int:trace_id>/screen.png', methods=['GET'])
def screenshot(trace_id):
    # backend tmp directory path
    tmp_path = os.path.join(SAT_HOME, 'satt', 'visualize', 'backend', 'tmp')
    trace_screenshot = tmp_path + str(trace_id) + ".png"
    print trace_screenshot

    # check if picture exists in tmp file already
    if not os.path.isfile(trace_screenshot):
        cur.execute("SELECT file_data FROM public.screenshots WHERE id = %s", (trace_id,))
        file_data = cur.fetchone()
        if file_data:
            if not os.path.exists(os.path.dirname(tmp_path)):
                os.makedirs(tmp_path)

            fp = open(trace_screenshot, 'wb')
            fp.write(str(file_data[0]))
            fp.close()
        else:
            abort(404)
    response = send_file(trace_screenshot, as_attachment=True, attachment_filename='screen.png')
    return response


def get_or_create_file(chunk, dst):
    if chunk == 0:
        f = file(dst, 'wb')
    else:
        f = file(dst, 'ab')
    return f


def allowed_file(filename):
    return '.' in filename and \
           filename.rsplit('.', 1)[1] in ALLOWED_EXTENSIONS


@app.route('/api/1/upload', methods=['GET'])
def upload_resume():
    flowCurrentChunkSize = request.args.get('flowCurrentChunkSize', False)
    flowFilename = request.args.get('flowFilename', False)
    if (flowCurrentChunkSize and flowFilename):
        filename = secure_filename(flowFilename)
        chunk = int(request.args.get('flowChunkNumber'))
        chunkname = os.path.join(UPLOAD_FOLDER, filename + '_' + str(chunk))
        if (os.path.isfile(chunkname)):
            if (os.path.getsize(chunkname) == int(flowCurrentChunkSize)):
                return json.dumps({"OK": 1})
            else:
                # if file size does not match, remove the file and request again
                os.path.remove(chunkname)
    abort(404)


@app.route('/api/1/upload', methods=['POST'])
def upload():
    if request.method == 'POST':
        for key, file in request.files.iteritems():
            if file:
                filename = secure_filename(request.form['flowFilename'])
                chunks = int(request.form['flowTotalChunks'])
                chunk = int(request.form['flowChunkNumber'])
                if DEBUG:
                    print "chunks {0} of chunk {1}".format(chunks, chunk)
                    print '***> ' + str(filename) + ' <***'
                    print str(request.form['flowFilename'])

                file.save(os.path.join(UPLOAD_FOLDER, filename + '_' + str(chunk).zfill(3)))

                # Just simple check, not really checking if those were uploaded succesfully
                if chunk == chunks:
                    read_files = sorted(glob.glob(os.path.join(UPLOAD_FOLDER, filename + '_*')))

                    with open(os.path.join(UPLOAD_FOLDER, filename), "wb") as outfile:
                        for f in read_files:
                            with open(f, "rb") as infile:
                                outfile.write(infile.read())

                    for tmp_files in glob.glob(os.path.join(UPLOAD_FOLDER, filename + '_*')):
                        os.remove(tmp_files)

                    print 'Quenue done'
                    # Setup up the work Queue
                    trace_id = status.create_id(os.path.join(UPLOAD_FOLDER, filename), filename)
                    if not sys.platform.startswith('win'):
                        result = queue.enqueue(worker.process_trace, trace_id, timeout=7200)
                        if not result:
                            print "Queuenue failed"

                    return json.dumps({"OK": 1, "info": "All chunks uploaded successful."})

    return json.dumps({"OK": 1, "info": "Chunk " + str(chunk) + " uploaded succesfully"})


@app.route('/', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def main():
    return app.send_static_file('index.html')


@app.route('/admin', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def admin():
    return app.send_static_file('index.html')


@app.route('/trace/<int:id>', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def trace_id(id):
    return app.send_static_file('index.html')


@app.route('/trace/components/<string:endpoint>/<string:endpoint2>', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def components(endpoint, endpoint2):
    print '/components/' + endpoint + '/' + endpoint2
    return app.send_static_file('components/' + endpoint + '/' + endpoint2)


@app.route('/admin/views/admin/<string:endpoint>', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def admin_view(endpoint):
    print '/views/admin/' + endpoint
    return app.send_static_file('views/admin/' + endpoint)


@app.route('/trace/<string:path>/<string:endpoint>', methods=['GET', 'POST', 'PATCH', 'PUT', 'DELETE'])
def trace(path, endpoint):
    return app.send_static_file(path + '/' + endpoint)


# Helper to get tsc tick
def get_tsc_tick(schema):
    cur.execute("select exists(select * from information_schema.tables where table_name=%s and table_schema=%s)", ('info', schema,))
    res = cur.fetchone()
    if res[0]:
        named_cur.execute("SELECT value from " + schema + ".info WHERE key = 'TSC_TICK'")
        if DEBUG:
            print named_cur.query
        tsc_tick = named_cur.fetchone()
        return tsc_tick[0]
    else:
        return 1330000


def create_bookmark():
    cur.execute("""create table if not exists public.bookmark
    (id serial, traceId int, title varchar(1024), description text, data text, PRIMARY KEY(id))""")


@app.route('/api/1/bookmark', methods=['GET'])
def getBookmarks():
    try:
        named_cur.execute("select * from public.bookmark")
    except:
        create_bookmark()
        named_cur.execute("select * from public.bookmark")
    data = named_cur.fetchall()

    return json.dumps(data)


@app.route('/api/1/bookmark/<int:bookmarkId>', methods=['GET'])
def getBookmarksById(bookmarkId):
    named_cur.execute("select * from public.bookmark where id = %s", (bookmarkId,))
    data = named_cur.fetchone()
    if data:
        return json.dumps(data)
    else:
        return jsonify(error=404, text='Bookmark was not found!'), 404


@app.route('/api/1/bookmark', methods=['POST', 'PATCH'])
def saveBookmark():
    if request.method == 'POST':
        data = request.get_json()
        try:
            named_cur.execute("INSERT INTO public.bookmark (traceId, title, description, data) VALUES (%s,%s,%s,%s) RETURNING id",
                              (data['traceId'], data['title'], data['description'], data['data'], ))
        except:
            create_bookmark()
            named_cur.execute("INSERT INTO public.bookmark (traceId, title, description, data) VALUES (%s,%s,%s,%s) RETURNING id",
                              (data['traceId'], data['title'], data['description'], data['data'],))
        insertedId = named_cur.fetchone()
        if DEBUG:
            print named_cur.query
    return json.dumps(insertedId)


@app.route('/api/1/bookmark/<int:bookmarkId>', methods=['DELETE'])
def deleteBookmark(bookmarkId):
    named_cur.execute("delete from public.bookmark where id = %s", (bookmarkId,))
    return json.dumps({"ok": "ok"})


@app.route('/api/1/bookmark', methods=['PUT'])
def updateBookmark():
    data = request.get_json()
    cur.execute("UPDATE public.bookmark SET title=%s, description=%s WHERE id=%s", (data['title'], data['description'], data['id'],))
    return json.dumps({"ok": "ok"})


@app.route('/api/1/traceinfo/<int:traceId>')
def traceinfo(traceId):
    schema = "t" + str(traceId)
    rows = []
    data = {}
    # Backward compatibility check, if info table exists
    cur.execute("select exists(select * from information_schema.tables where table_name=%s and table_schema=%s)", ('info',schema,))
    res = cur.fetchone()
    if res[0]:
        named_cur.execute("""SELECT * FROM """+schema+""".info order by key""")
        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()

    if not rows:
        rows = [{"key":"TSC_TICK", "info":"Time Stamp Counter tick", "value":1330000},
                {"key":"FSB_MHZ", "info":"Front-side Bus MHz", "value":200}]

    data['infos'] = rows

    named_cur.execute("select * from public.traces where id=%s", (traceId, ))
    rows = named_cur.fetchall()
    data['trace'] = rows[0]

    return json.dumps(data, default=dthandler)

# Helper funtion to merge insflow arrays
# - Will check last existing line and it's timestamps
#   and merge only after that timestamps. So that three formation is kept
def merge_insflow_overflow_sections(old_rows,new_rows):
    ret_rows = []
    if len(old_rows) > 0:
        old_start_ts = old_rows[-1][1]
        old_start_duration = old_rows[-1][3] + old_rows[-1][4]
        old_end = old_start_ts + old_start_duration
        for r in new_rows:
            # Filter our functions that should be inside old last function
            if old_end <= r[1]:
               ret_rows = ret_rows + [r]
        return old_rows + ret_rows
    else:
        return old_rows + new_rows

@app.route('/api/1/insflownode/<int:traceId>/<string:pid>/<int:start>/<int:end>/<int:level>', methods=['GET', 'POST'])
def graph_insflownode(traceId, pid, start, end, level):
    schema = "t" + str(traceId)
    # Searching next level from 'start' ts onwards. We can include 'start' ts
    #  into search because we anyway search one step deeper in stack, so we
    #  don't include the parent level item and cause duplicate line.

    if end == 0:

        cur.execute("""SELECT count(*)
            FROM """+schema+""".ins
            WHERE thread_id = %s and level = %s and ts > %s
            """,(pid,level,start,))
        max_row_count = cur.fetchone()
        cur.execute("""SELECT ins.id, ins.ts, ins.level, ins.ts_oot, ins.ts_int, ins.ins_count, ins.call, ins.cpu, ins.thread_id, module, sym.symbol
            FROM """+schema+""".ins
            JOIN """+schema+""".module as mod ON (mod.id = module_id)
            JOIN """+schema+""".symbol as sym ON (sym.id = symbol_id)
            WHERE thread_id = %s and level = %s and ts >= %s
            ORDER BY ts
            LIMIT %s""",(pid,level,start,INS_MORE_LIMIT,))
        rows = cur.fetchall()

        if DEBUG:
            print cur.query
    else:
        cur.execute("""SELECT count(*)
            FROM """+schema+""".ins
            WHERE thread_id = %s and level = %s and ts > %s and ts <= %s
            """,(pid,level,start,end))
        max_row_count = cur.fetchone()
        cur.execute("""SELECT ins.id, ins.ts, ins.level, ins.ts_oot, ins.ts_int, ins.ins_count, ins.call, ins.cpu, ins.thread_id, module, sym.symbol
                    FROM """+schema+""".ins
                    JOIN """+schema+""".module as mod ON (mod.id = module_id)
                    JOIN """+schema+""".symbol as sym ON (sym.id = symbol_id)
                    WHERE thread_id = %s and level = %s and ts >= %s and ts <= %s
                    ORDER BY ts
                    LIMIT %s""",(pid,level,start,end,INS_MORE_LIMIT,))
        rows = cur.fetchall()

        if DEBUG:
            print cur.query

    data = []
    for r in rows:
        call = r[6]
        if r[6] == "r":
            call = 'e'
        data.append({"id":r[0],"ts":r[1],"l":r[2],"of":r[3],"it":r[4],"in":r[5],"cl":call,"cpu":r[7],"tgid":r[8],"mod":r[9],"sym": r[10],} )

    if max_row_count[0] > INS_MORE_LIMIT:
        # Add info to data
        data.append({"id":r[0],"ts":r[1],"l":r[2],"of":0,"it":0,"in":0,"cl":"m","row_count":max_row_count[0],"cpu":0,"tgid":0,"mod":0,"sym":0,} )

    return jsonify({"data":data})


# thread 0/0
@app.route('/api/1/insflow/<int:traceId>/<string:pid>/<int:start>/<int:end>', methods=['GET', 'POST'])
def graph_insflow(traceId, pid, start, end):
    schema = "t" + str(traceId)

    cur.execute("""select Min(level) from """+schema+""".ins where thread_id = %s and ts >= %s and ts <= %s """,(pid,start,end,))
    min_level_in_set = cur.fetchone()

    if DEBUG:
        print cur.query

    """ CALCULATE DURATION FOR THE CALLS
    SELECT CASE WHEN call = 'c' THEN lead(ts) over(partition by level order by ts)
    ELSE lead(ts) over(order by ts) END
    - ts -1 as duration,
    """

    overflow_rows = []
    rows = []

    cur.execute("""select id from """+schema+""".symbol where symbol = 'overflow' """,(pid,start,end,))
    overflow_symbol_id = cur.fetchone()
    if overflow_symbol_id:
        print "OVERFLOW ID"
        overflow_symbol_id = overflow_symbol_id[0]

        # get timestamps for the overflows
        named_cur.execute("""SELECT * FROM """+schema+""".ins
            WHERE thread_id = %s AND ts >= %s AND ts <= %s and symbol_id =%s
        ORDER BY ts
        LIMIT %s;""",(pid,start,end,overflow_symbol_id, INS_MORE_LIMIT))

        overflow_rows = []
        if DEBUG:
            print named_cur.query

        overflow_rows = named_cur.fetchall()

    if len(overflow_rows):
        for i, r in enumerate(overflow_rows):
            if i == 0:
                start_time = start
                end_time = r.ts
            elif i == len(overflow_rows)-1:
                start_time = r.ts
                end_time = end
            else:
                start_time = end_time
                end_time = r.ts

            cur.execute("""SELECT ins.id, ins.ts, ins.level, ins.ts_oot, ins.ts_int, ins.ins_count, ins.call, ins.cpu, ins.thread_id, module, sym.symbol
            FROM
            (select *, min(level) over (order by ts) as min_level from
            """+schema+""".ins as s2 where thread_id = %s AND ts >= %s AND ts <= %s
            ) ins
            JOIN """+schema+""".module as mod ON (mod.id = module_id)
            JOIN """+schema+""".symbol as sym ON (sym.id = symbol_id)
            WHERE level <= min_level
            ORDER BY ts
            LIMIT %s;""",(pid,start_time,end_time,INS_MORE_LIMIT))
            #if DEBUG:
                #print cur.query
            rows = merge_insflow_overflow_sections(rows, cur.fetchall())
            if len(rows) >= INS_MORE_LIMIT:
                break
    else:
        cur.execute("""SELECT ins.id, ins.ts, ins.level, ins.ts_oot, ins.ts_int, ins.ins_count, ins.call, ins.cpu, ins.thread_id, module, sym.symbol
        FROM
        (select *, min(level) over (order by ts) as min_level from
        """+schema+""".ins as s2 where thread_id = %s AND ts >= %s AND ts <= %s
        ) ins
        JOIN """+schema+""".module as mod ON (mod.id = module_id)
        JOIN """+schema+""".symbol as sym ON (sym.id = symbol_id)
        WHERE level <= min_level
        ORDER BY ts
        LIMIT %s;""",(pid,start,end,INS_MORE_LIMIT))
        rows = rows + cur.fetchall()

    data = []
    found_min_level = 0xFFFFFF
    for r in rows:
        data.append({"id":r[0],"ts":r[1],"l":r[2],"of":r[3],"it":r[4],"in":r[5],"cl":r[6],"cpu":r[7],"tgid":r[8],"mod":r[9],"sym":r[10],} )
        if found_min_level > r[2]:
            found_min_level = r[2]

    if len(rows) >= INS_MORE_LIMIT:
        # Check if bottom of the call stack was found to change behavior of more button in UI
        if min_level_in_set[0] < found_min_level:
            data.append({"id":r[0],"ts":r[1],"l":r[2],"of":0,"it":0,"in":0,"cl":"m","row_count":"???","cpu":0,"tgid":0,"mod":0,"sym":0,"min_level":min_level_in_set,} )
        else:
            data.append({"id":r[0],"ts":r[1],"l":r[2],"of":0,"it":0,"in":0,"cl":"m","row_count":"???","cpu":0,"tgid":0,"mod":0,"sym":0,} )

    return jsonify({"min_level":found_min_level,"data":data})

################################################################
#
# Statistics grouping by threads
#
################################################################
@app.route('/api/1/statistics/groups/thread/<int:traceId>/<int:start>/<int:end>', methods=['GET', 'POST'])
def statistics_groups_thread(traceId, start, end):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""select round((ins::real * 100 / ( select sum(sum) from """ +schema +""".graph where gen_ts >= %s and gen_ts <= %s ))::numeric , 4)::real as percent, * from (
        select sum(sum) as ins, tgid, pid, name from """ +schema +""".graph
        join """+schema+""".tgid on thread_id = tgid.id
        where gen_ts >= %s and gen_ts <= %s
        group by tgid, name, pid
        order by 1 desc
        ) s1""",(start,end,start,end,))

        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()
        rows.insert(0,{"id":"0", "percent":"100","name":"Showing all Threads"})
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print e
        return jsonify({"ERROR":100})

################################################################
#
# Statistics grouping by Process
#
################################################################
@app.route('/api/1/statistics/groups/process/<int:traceId>/<int:start>/<int:end>', methods=['GET', 'POST'])
def statistics_groups_process(traceId, start, end):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""select round((ins::real * 100 / ( select sum(sum) from """ +schema +""".graph where gen_ts >= %s and gen_ts <= %s ))::numeric , 4)::real as percent, * from (
        select sum(sum) as ins, tgid, name from """ +schema +""".graph
        join """+schema+""".tgid on thread_id = tgid.id
        where gen_ts >= %s and gen_ts <= %s
        group by tgid, name
        order by 1 desc
        ) s1""",(start,end,start,end,))

        if DEBUG:
            print named_cur.query

        rows = named_cur.fetchall()
        rows.insert(0,{'id':0, 'percent':100,'name':'Showing all Processes'})
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print e
        return jsonify({"ERROR":100})

################################################################
#
# Statistics grouping by Module
#
################################################################
@app.route('/api/1/statistics/groups/module/<int:traceId>/<int:start>/<int:end>', methods=['GET', 'POST'])
def statistics_groups_module(traceId, start, end):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""
        select round((ins::real * 100 / ( select sum(ins_count) from """ +schema +""".ins as s2 where ts >= %s and ts <= %s))::numeric , 4)::real as percent, * from (
            select sum(ins_count) as ins, module as name, module_id from """ +schema +""".ins
            join """ +schema +""".module on module.id = ins.module_id
            where ts >= %s and ts <= %s
            group by module, module_id
            order by 1 desc
            limit 100 )s1""",(start,end,start,end,))

        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()
        rows.insert(0,{"id":"0", "percent":'100',"name":"Showing all Modules"})
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print "problem".format(e)

################################################################
#
# Statistics items from a process
#
################################################################
@app.route('/api/1/statistics/process/<int:traceId>/<int:start>/<int:end>/<int:tgid>', methods=['GET', 'POST'])
def statistics_process(traceId, start, end, tgid):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""
            select symbol,
            sum(ins_count) as ins,
            sum(
            CASE when call = 'c' THEN
            1
            END
            ) as call_count,
            sum(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as in_thread,
            round ( avg(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) )::real as avg_in_thread,
            sum (
            CASE WHEN call = 'e' THEN
            ts_int
            END
            ) as in_abs_thread,
            min(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as min_in_thread,
            max(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as max_in_thread,
            sum(
            CASE WHEN call = 'c' THEN
            ts_oot
            END
            ) as out_thread,
            symbol_id from """ + schema + """.ins
            join """ + schema + """.tgid on thread_id = tgid.id
            join """ + schema + """.symbol on symbol_id = symbol.id
            where tgid = %s and ts >= %s and ts <= %s
            group by symbol_id, symbol
            order by ins desc
            limit 1000""",(tgid,start,end))

        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print "problem".format(e)

################################################################
#
# Statistics items from a thread
#
################################################################
@app.route('/api/1/statistics/thread/<int:traceId>/<int:start>/<int:end>/<int:pid>', methods=['GET', 'POST'])
def statistics_thread(traceId, start, end, pid):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""
            select symbol,
            sum(ins_count) as ins,
            sum(
            CASE when call = 'c' THEN
            1
            END
            ) as call_count,
            sum(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as in_thread,
            round ( avg(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) )::real as avg_in_thread,
            sum (
            CASE WHEN call = 'e' THEN
            ts_int
            END
            ) as in_abs_thread,
            min(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as min_in_thread,
            max(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as max_in_thread,
            sum(
            CASE WHEN call = 'c' THEN
            ts_oot
            END
            ) as out_thread,
            symbol_id from """ + schema + """.ins
            join """ + schema + """.tgid on thread_id = tgid.id
            join """ + schema + """.symbol on symbol_id = symbol.id
            where pid = %s and ts >= %s and ts <= %s
            group by symbol_id, symbol
            order by ins desc
            limit 1000""",(pid,start,end))

        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print "problem".format(e)

################################################################
#
# Statistics items from a module
#
################################################################
@app.route('/api/1/statistics/module/<int:traceId>/<int:start>/<int:end>/<int:module_id>', methods=['GET', 'POST'])
def statistics_module(traceId, start, end, module_id):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""
            select symbol,
            sum(ins_count) as ins,
            sum(
            CASE when call = 'c' THEN
            1
            END
            ) as call_count,
            sum(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as in_thread,
            round ( avg(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) )::real as avg_in_thread,
            sum (
            CASE WHEN call = 'e' THEN
            ts_int
            END
            ) as in_abs_thread,
            min(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as min_in_thread,
            max(
            CASE WHEN call = 'c' THEN
            ts_int
            END
            ) as max_in_thread,
            sum(
            CASE WHEN call = 'c' THEN
            ts_oot
            END
            ) as out_thread,
            symbol_id from """ + schema + """.ins
            join """ + schema + """.tgid on thread_id = tgid.id
            join """ + schema + """.symbol on symbol_id = symbol.id
            where module_id = %s and ts >= %s and ts <= %s
            group by symbol_id, symbol
            order by ins desc
            limit 1000""",(module_id,start,end))

        if DEBUG:
            print named_cur.query
        rows = named_cur.fetchall()
        return json.dumps({'data':rows}, use_decimal=True)
    except Exception, e:
        print "problem".format(e)

################################################################
#
# Statistics get callers
#
################################################################
#
# CREATE INDEX t66_multi2_idx on t66.ins(symbol_id, call, level, ts)
#
# CREATE INDEX t66_multi_idx on t66.ins(symbol_id, call, level, ts)
#
@app.route('/api/1/statistics/callers/<int:traceId>/<int:start>/<int:end>/<int:tgid>/<int:symbol_id>', methods=['GET', 'POST'])
def statistics_callers(traceId, start, end, tgid, symbol_id):
    schema = "t" + str(traceId)
    try:
        named_cur.execute("""
            select count(*) as call_count, avg(ts_int) as avg_ts_int, symbol_id, symbol from """+schema+""".ins as A
                join """+schema+""".tgid on thread_id = tgid.id
                join """+schema+""".symbol on symbol_id = symbol.id
            where call = 'c' and tgid = %s and symbol_id <> %s
            and exists ( select * from """+schema+""".ins as B where call = 'c' and level = A.level +1 and symbol_id = %s and ts > A.ts and ts < (A.ts + A.ts_int) )
            group by symbol_id, symbol
            order by 1
            limit 50
            """,(tgid,symbol_id,symbol_id,))
        if DEBUG:
            print named_cur.query
        return "hello"
        #return jsonify({"data":rows})
    except Exception, e:
        print "problem".format(e)

################################################################
#
# Delete trace permanetly
#
################################################################
@app.route('/api/1/trace/<int:traceId>', methods=['DELETE'])
def delete_trace(traceId):
    schema = "t" + str(traceId)
    try:
        cur.execute("DROP SCHEMA IF EXISTS "+schema+" CASCADE;")
        cur.execute("DELETE FROM public.traces WHERE id = %s",(traceId,))
        db.commit()
        return jsonify({"status":"ok"})
    except Exception, e:
        print "error ".format(e)
        return jsonify({"status":"error"})

################################################################
#
# Handle Admin Post to modify trace names
#
################################################################
@app.route('/api/1/trace/<int:traceId>', methods=['POST'])
def post_trace(traceId):
    js = request.json
    try:
        for key in js:
            cur.execute("UPDATE public.traces SET " + key + "=%s WHERE id = %s", (js[key], traceId,))
            print key, 'corresponds to', js[key]
            db.commit()
        return jsonify({"status":"ok"})
    except Exception, e:
        print "error ".format(e)
        return jsonify({"status":"error"})

################################################################
#
# Handle Admin / Public Get list of trace
#
################################################################
@app.route('/api/1/trace/<int:traceId>', methods=['GET'])
def get_trace(traceId):
    named_cur.execute("SELECT * FROM public.traces WHERE id = %s",(traceId,))
    row = named_cur.fetchone()
    return json.dumps(row, default= dthandler)


@app.route('/api/1/traces/', methods=['GET', 'POST'])
def get_traces():
    named_cur.execute("SELECT * FROM public.traces order by id")
    results = named_cur.fetchall()
    return json.dumps(results, default= dthandler)

################################################################
#
# getFullAvgGraphPerCpu / private call
#
################################################################
def getFullAvgGraphPerCpu(schema, cpu, start_time, end_time, time_slice):
    tsc_tick = get_tsc_tick(schema)
    cur.execute("""
    select x, thread_id, round((ins::numeric*1000/%s)/(%s::numeric/1000000),1) from (
    select ts / %s as x,thread_id,sum as ins from (
    select (gen_ts/%s)*%s as ts, thread_id, sum(sum), count(*) over (partition by gen_ts/%s) as ts_count
    from """+schema+""".graph
    where cpu = %s
    group by gen_ts/%s, thread_id
    order by ts
    ) s where ts_count = 1 or sum <> 0
    ) s1 """,(time_slice, tsc_tick, time_slice, time_slice,time_slice,time_slice,cpu,time_slice,))

    if DEBUG:
        print cur.query

    results = cur.fetchall()
    traces = {}
    row_count = -1
    new_row = -1
    for row in results:
        if not row[0] == new_row:
            new_row = row[0]
            row_count = row_count + 1
        if not row[1] in traces:
            # thread_id HACK to support 0/0 cur.execute("SELECT * from "+schema+".tgid where tgid = %s", (row[1],))
            # if row[1] == '0/0':
            #     cur.execute("SELECT * from "+schema+".tgid where tgid = %s", (0,))
            # elif row[1] == '0/1':
            #     cur.execute("SELECT * from "+schema+".tgid where tgid = %s", (0,))
            # else:
            named_cur.execute("SELECT * from "+schema+".tgid where id = %s", (row[1],))
            if DEBUG:
                print named_cur.query

            name = named_cur.fetchone()
            traces.update({row[1]: {"n":name.name,"id":name.id,"p":name.pid,"tgid":name.tgid,"color":name.color,"data":{} }})

        traces[row[1]]['data'].update({row[0]:row[2]})

    return traces, row_count

################################################################
#
# getFullGraphPerCpu / private call
#
################################################################
def getFullGraphPerCpu(schema, cpu, start_time, end_time, time_slice):
    cur.execute("""select gen_ts, thread_id, sum(part2)::int from
            (
            select * ,
            -- FINAL CALCULATION IS DONE HERE!!!!
                CASE WHEN row_number() over(partition by id ) = 1 AND (gen_ts + %s) < (end_ts) THEN
                -- THIS NEED Percent calcutation to figure out exec size
                    -- ( Amount * Multiplier ) / slice
                    --Multiplier
                    (ins_count * (%s - ( great - gen_ts))::bigint) / (len +1)::real

                ELSE
                -- Check if we are in row 2 or so
                    -- ( Amount * Multiplier ) / slice
                    -- OPTION 1 or
                    --(ins_count * CASE WHEN end_ts - great +1 >= %s THEN %s ELSE end_ts - great +1 END) / (len +1)
                    -- OPTION 2 check witch one is faster
                    CASE WHEN row_number() over(partition by id ) = 1 THEN
                        ins_count::real
                    ELSE
                        (ins_count * CASE WHEN end_ts - great +1 >= %s THEN %s ELSE end_ts - great +1 END::bigint) / (len +1)::real
                    END
                END
                as part2

             from
            (
            select *, greatest(ts, gen_ts) as great from
            (
            select  end_ts - ts as len,
                *,
                generate_series(ts - (ts %% %s), end_ts, %s) as gen_ts
            from
            (
            --SELECT lead(ts) over (order by id) -1 as end_ts ,*
            SELECT lead(ts) over () -1 as end_ts ,*
            FROM """+schema+""".ins
            WHERE cpu = %s
            ORDER BY ID
            ) s2
            ) s3
            ) s4
            ) s5
            group by 1,2
            order by gen_ts
        """,(time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,cpu))

    results = cur.fetchall()
    traces = {}
    row_count = -1
    new_row = -1
    for row in results:
        if not row[0] == new_row:
            new_row = row[0]
            row_count = row_count + 1
        if not row[1] in traces:
            named_cur.execute("SELECT * from "+schema+".tgid where id = %s", (row[1],))

            name = named_cur.fetchone()
            traces.update({row[1]: {"n":name.name,"id":name.id,"p":name.pid,"tgid":name.tgid,"tid":name.id,"color":name.color,"data":{} }} )
        traces[row[1]]['data'].update({row_count:row[2]})
    #print row_count
    return traces, row_count

@app.route('/api/1/graph/<int:traceId>/full/<int:pixels>', methods=['GET', 'POST'])
def graph_full(traceId, pixels):
    if request.method == 'GET':
        cur.execute("select cpu_count from public.traces where id = %s",(traceId,))
        cpus = cur.fetchone()
        schema = "t" + str(traceId)
        cur.execute("select min(gen_ts), max(gen_ts) from "+schema+".graph")
        tmp = cur.fetchone()
        start_time=tmp[0]
        end_time=tmp[1]
        time_slice = (end_time - start_time -1) / pixels
        if DEBUG:
            print "Graph Full"
            print "Start=%d"%start_time
            print "End=%d"%end_time
            print "timeslice=%d"%time_slice
            print "pixels Wanted=%d"%pixels
        traces = {}
        for cpu in range(cpus[0]):
            traces[cpu], row_count = getFullAvgGraphPerCpu(schema, cpu, start_time, end_time, time_slice)

        cpu_array = {}
        for cpu in range(cpus[0]):
            traces_array = []
            for trace in traces[cpu]:
                traces_array.append(traces[cpu][trace])
            cpu_array["cpu"+str(cpu)] = traces_array

        return jsonify({"start":start_time,
                        "end":end_time,
                        "cpus": cpus[0],
                        "pixels":pixels,
                        "traces": cpu_array,
                        })
    elif request.method == 'POST':
        return "ECHO: POST\n"


def getGraphAvgPerCpu(schema, cpu, start_time, end_time, time_slice):
    tsc_tick = get_tsc_tick(schema)
    cur.execute("""
    select x, thread_id, round((ins::numeric*1000/%s)/(%s::numeric/1000000),1) from (
    select ( ts - %s ) / %s as x,thread_id,sum as ins from (
    select (gen_ts/%s)*%s as ts, thread_id, sum(sum), count(*) over (partition by gen_ts/%s) as ts_count
    from """+schema+""".graph
    where cpu = %s and gen_ts >= %s and gen_ts <= %s
    group by gen_ts/%s, thread_id
    order by ts
    ) s where ts_count = 1 or sum <> 0
    ) s1 """,(time_slice, tsc_tick, start_time, time_slice, time_slice,time_slice,time_slice,cpu,start_time, end_time, time_slice,))

    if DEBUG:
        print cur.query

    results = cur.fetchall()
    traces = {}
    row_count = -1
    new_row = -1
    for row in results:
        if not row[0] == new_row:
            new_row = row[0]
            row_count = row_count + 1
        if not row[1] in traces:
            named_cur.execute("SELECT * from "+schema+".tgid where id = %s", (row[1],))

            name = named_cur.fetchone()
            traces.update({row[1]: {"n":name.name,"id":name.id,"p":name.pid,"tgid":name.tgid,"tid":name.id,"color":name.color,"data":{} }} )

        traces[row[1]]['data'].update({row[0]:row[2]})
    return traces, row_count

def getGraphPerCpu(schema, cpu, start_time, end_time, time_slice, gen_start, gen_end):
    tsc_tick = get_tsc_tick(schema)
    cur.execute("""
        select x, thread_id, round((ins::numeric*1000/%s)/(%s::numeric/1000000),1) from (
        select ( gen_ts - %s) / %s as x , thread_id, sum(part2)::int as ins from
            (
            select * ,
            -- FINAL CALCULATION IS DONE HERE!!!!
                CASE WHEN row_number() over(partition by id ) = 1 AND (gen_ts + %s) < (end_ts) THEN
                -- THIS NEED Percent calcutation to figure out exec size
                    -- ( Amount * Multiplier ) / slice
                    --Multiplier
                    (ins_count * (%s - ( great - gen_ts))::bigint) / (len +1)::real

                ELSE
                -- Check if we are in row 2 or so
                    -- ( Amount * Multiplier ) / slice
                    -- OPTION 1 or
                    --(ins_count * CASE WHEN end_ts - great +1 >= %s THEN %s ELSE end_ts - great +1 END) / (len +1)
                    -- OPTION 2 check witch one is faster
                    CASE WHEN row_number() over(partition by id ) = 1 THEN
                        ins_count::real
                    ELSE
                        (ins_count * CASE WHEN end_ts - great +1 >= %s THEN %s ELSE end_ts - great +1 END::bigint) / (len +1)::real
                    END
                END
                as part2

             from
            (
            select *, greatest(ts, gen_ts) as great from
            (
            select  end_ts - ts as len, *, generate_series(ts - (ts %% %s), end_ts, %s) as gen_ts
            from
            (
            --SELECT lead(ts) over (order by id) -1 as end_ts ,*
            --SELECT lead(ts) over () -1 as end_ts ,* FROM """+schema+""".ins
            SELECT lead(ts) over () -1 as end_ts ,* FROM
            (
                ( SELECT  * FROM """+schema+""".ins where cpu = %s and ts < %s order by id DESC LIMIT 1 )
                UNION ALL
                ( SELECT * FROM """+schema+""".ins where cpu = %s and ts >= %s and ts <= %s order by id)
                UNION ALL
                ( SELECT * FROM """+schema+""".ins where cpu = %s and ts > %s order by id ASC LIMIT 2)
            ) s1
            ) s2
            ) s3
            where gen_ts >= %s and gen_ts <= %s
            ) s4
            ) s5
            group by 1,2
            order by 1
            ) s6
        """,(time_slice, tsc_tick, start_time, time_slice, time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,time_slice,cpu, start_time,cpu, start_time,end_time, cpu, end_time, start_time,end_time))

    if DEBUG:
        print cur.query

    results = cur.fetchall()
    traces = {}
    row_count = -1
    new_row = -1
    for row in results:
        if not row[0] == new_row:
            new_row = row[0]
            row_count = row_count + 1
        if not row[1] in traces:
            named_cur.execute("SELECT * from "+schema+".tgid where id = %s", (row[1],))

            name = named_cur.fetchone()
            traces.update({row[1]: {"n":name.name,"id":name.id,"p":name.pid,"tgid":name.tgid,"color":name.color,"data":{} }} )
        traces[row[1]]['data'].update({row[0]:row[2]})

    return traces, row_count

@app.route('/api/1/graph/<int:traceId>/<int:pixels>/<int:start>/<int:end>', methods=['GET', 'POST'])
def graph(traceId, pixels, start, end):
    if request.method == 'GET':
        cur.execute("select cpu_count from public.traces where id = %s",(traceId,))
        cpus = cur.fetchone()

        schema = "t" + str(traceId)

        start_time=start
        end_time=end
        time_slice = (end_time - start_time)
        if (time_slice <= pixels):
            new_pixels = 2 * pixels - time_slice
            start_time = start_time - new_pixels / 2
            end_time   = end_time + new_pixels / 2 + 1
            time_slice = 2
        else:
            time_slice = time_slice / pixels

        gen_start = start_time
        gen_end = start_time + ( pixels * time_slice )
        gen_timeslice = (gen_end-gen_start)/pixels

        traces = {}

        if DEBUG:
            print "start_time             =",(start,)
            print "end_time               =",(end,)
            print "pixels                 =",(pixels,)
            print "time in 1 pixel        =",((end_time - start_time)/pixels,)
            print "(end_time - start_time)=",(end_time - start_time,)
            print "gen_start              =",(gen_start,)
            print "gen_end                =",(gen_end,)
            print "gentime in 1 pixel     =",((gen_end-gen_start)/pixels,)
            print "new pixels             =",((gen_end-gen_start)/((gen_end-gen_start)/pixels),)

        # Timeslice bigger than 10ms go for AVG view
        if ( end_time - start_time ) / 1596 > 10000:
            if DEBUG:
                print "*** AVG view"
                print "start_time             =",(start_time,)
                print "end_time               =",(end_time,)
            for cpu in range(cpus[0]):
                traces[cpu], row_count = getGraphAvgPerCpu(schema, cpu, gen_start, gen_end, gen_timeslice)
        else:
            for cpu in range(cpus[0]):
                traces[cpu], row_count = getGraphPerCpu(schema, cpu, start_time, end_time, time_slice, gen_start, gen_end)

        if DEBUG:
            print "Graph - Zoom"
            print "Data between=%d"%(end_time - start_time)
            print "Start=%d"%start_time
            print "End=%d"%end_time
            print "timeslice=%d"%time_slice
            print "pixels Wanted=%d"%pixels
            if time_slice <= 0:
                return jsonify({"status":False})

        cpu_array = {}
        for cpu in range(cpus[0]):
            traces_array = []
            for trace in traces[cpu]:
                traces_array.append(traces[cpu][trace])
            cpu_array["cpu"+str(cpu)] = traces_array

        return jsonify({"start":gen_start,
                        "end":gen_end,
                        "cpus": cpus[0],
                        "pixels":pixels,
                        "traces": cpu_array})
    elif request.method == 'POST':
        return "ECHO: POST\n"
#
#  SEARCH
#
#  Search matching symbol name - limiting to 100 results
#
@app.route('/api/1/search/<int:traceId>', methods=['GET', 'POST'])
def search(traceId):
    schema = "t" + str(traceId)
    if "search" in request.json and len(request.json['search']):
        named_cur.execute("""SELECT symbol.id, symbol from """+schema+""".symbol
                             WHERE symbol LIKE %s
                             ORDER BY symbol
                             LIMIT 100""",('%' + request.json['search'] + '%',))

        r = [dict((named_cur.description[i][0], value) \
               for i, value in enumerate(row)) for row in named_cur.fetchall()]
        if DEBUG:
            print named_cur.query
        return jsonify({"data":r})

    return jsonify({"error":"error"})
#
#  2.nd phase Search hit count for following symbol ids
#
@app.route('/api/1/search/hits/<int:traceId>', methods=['GET', 'POST'])
def search_hits(traceId):
    schema = "t" + str(traceId)
    if "ids" in request.json and len(request.json['ids']):
        ids = ""
        comma = ""

        for id in request.json['ids']:
            ids = ids + comma + str(id)
            comma = ", "
        print ids
        named_cur.execute("""SELECT symbol.id, count(*) as hits from """+schema+""".symbol
                LEFT JOIN """+schema+""".ins on symbol.id = ins.symbol_id
                WHERE symbol.id IN %s
                AND call = 'c'
                GROUP BY 1""",(tuple(request.json['ids']),))
        if DEBUG:
            print named_cur.query
        r = [dict((named_cur.description[i][0], value) \
               for i, value in enumerate(row)) for row in named_cur.fetchall()]
        return jsonify({"data":r})
    return jsonify({"error":"error"})

#
#  3.rd phase Search places for search hits
#
@app.route('/api/1/search/<int:traceId>/<int:pixels>/<int:start_time>/<int:end_time>/<int:symbol_id>', methods=['GET', 'POST'])
def search_full(traceId,pixels,start_time,end_time,symbol_id):
    schema = "t" + str(traceId)
    time_slice = (end_time - start_time -1) / pixels
    if DEBUG:
        print "Search Full"
        print "Start=%d"%start_time
        print "End=%d"%end_time
        print "timeslice=%d"%time_slice
        print "pixels Wanted=%d"%pixels

    named_cur.execute("""SELECT (ts/%s)*%s as ts, count(*) as hits, cpu
            from """+schema+""".ins
            full join
                (select ts from generate_series(%s,%s,%s) ts) s1
                using (ts)
            WHERE symbol_id = %s and ts > %s and ts < %s and call = 'c'
            group by 1,3
            order by ts""",(time_slice,time_slice,start_time,end_time,time_slice,symbol_id,start_time-1,end_time+1,))

    if DEBUG:
        print named_cur.query
    rows = named_cur.fetchall()

    r = [dict((named_cur.description[i][0], value) \
           for i, value in enumerate(row)) for row in rows]
    return jsonify({"data":r})

#
#  Search overflows
#
@app.route('/api/1/search/overflow/<int:traceId>/<int:pixels>/<int:start_time>/<int:end_time>', methods=['GET', 'POST'])
def search_full_overflow(traceId,pixels,start_time,end_time):
    schema = "t" + str(traceId)
    time_slice = (end_time - start_time -1) / pixels
    if DEBUG:
        print "Search Full"
        print "Start=%d"%start_time
        print "End=%d"%end_time
        print "timeslice=%d"%time_slice
        print "pixels Wanted=%d"%pixels

    # TODO symbol like overflow is SLOW!!!!! Change look for ID so that it can be indexed
    named_cur.execute("""SELECT id from """+schema+""".symbol where symbol LIKE 'overflow'""")
    symbol_id = named_cur.fetchone()

    # Same as below, but now with cpu info
    named_cur.execute("""SELECT (ts/%s)*%s as ts, count(*) as hits, cpu
            from """+schema+""".ins
            full join
                (select ts from generate_series(%s,%s,%s) ts) s1
                using (ts)
            WHERE symbol_id = %s and ts > %s and ts < %s
            group by 1,3
            order by ts""",(time_slice,time_slice,start_time,end_time,time_slice,symbol_id,start_time-1,end_time+1,))

    if DEBUG:
        print named_cur.query
    rows = named_cur.fetchall()

    r = [dict((named_cur.description[i][0], value) \
           for i, value in enumerate(row)) for row in rows]
    return jsonify({"data":r})

#
#  Search overflows
#
@app.route('/api/1/search/lost/<int:traceId>/<int:pixels>/<int:start_time>/<int:end_time>', methods=['GET', 'POST'])
def search_full_lost(traceId,pixels,start_time,end_time):
    schema = "t" + str(traceId)
    time_slice = (end_time - start_time -1) / pixels
    if DEBUG:
        print "Search Full"
        print "Start=%d"%start_time
        print "End=%d"%end_time
        print "timeslice=%d"%time_slice
        print "pixels Wanted=%d"%pixels

    named_cur.execute("""SELECT id from """+schema+""".symbol where symbol LIKE 'lost'""")
    symbol_id = named_cur.fetchone()

    # Same as below, but now with cpu info
    named_cur.execute("""SELECT (ts/%s)*%s as ts, count(*) as hits, cpu
            from """+schema+""".ins
            full join
                (select ts from generate_series(%s,%s,%s) ts) s1
                using (ts)
            WHERE symbol_id = %s and ts > %s and ts < %s
            group by 1,3
            order by ts""",(time_slice,time_slice,start_time,end_time,time_slice,symbol_id,start_time-1,end_time+1,))

    if DEBUG:
        print named_cur.query
    rows = named_cur.fetchall()

    r = [dict((named_cur.description[i][0], value) \
           for i, value in enumerate(row)) for row in rows]
    return jsonify({"data":r})

################################################################
#
# cbr / Core Bus Ratio query
#
################################################################
@app.route('/api/1/cbr/<int:traceId>/<int:start_time>/<int:end_time>', methods=['GET', 'POST'])
def cbr(traceId,start_time,end_time):
    schema = "t" + str(traceId)
    if end_time == 0:
        named_cur.execute("""select * from """+schema+""".cbr""")
    else:
        named_cur.execute("""select * from """+schema+""".cbr where ts >= %s and ts <= %s""",(start_time,end_time,))
    rows = named_cur.fetchall()

    r = [dict((named_cur.description[i][0], value) \
           for i, value in enumerate(row)) for row in rows]
    return jsonify({"data":r})

if __name__ == '__main__':
    app.run()
