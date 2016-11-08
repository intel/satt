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
import psycopg2
import psycopg2.extras
import colorsys
from struct import pack
from io import BytesIO
import glob
import os
import argparse
from time import gmtime, strftime
import status as stat
import pickle
import sys

status = stat.getStatus()

conn = psycopg2.connect(
    dbname=status.getDbConfig('dbname'),
    user=status.getDbConfig('user'),
    password=status.getDbConfig('password'))

curs = conn.cursor()
named_cur = conn.cursor(cursor_factory=psycopg2.extras.NamedTupleCursor)

cpu_count = 0
insert_id = 0


def prepare_text(dat):
    cpy = BytesIO()
    for row in dat:
        cpy.write('\t'.join([repr(x) for x in row]) + '\n')
    return(cpy)


def get_separator(filename):
    separator = '|'
    with open(filename, 'r') as inF:
        for line in inF:
            if ';' in line:
                separator = ';'
                break
    return separator


def importCSV(insert_id, fn, fpath):
    global cpu_count
    schema = 't' + str(insert_id)

    curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                 '.tgid (id serial, tgid int4, pid int4, name varchar(256), color varchar(7), PRIMARY KEY(id))')
    filename = fn + '.satp'
    file_columns = ('id', 'tgid', 'pid', 'name')
    curs.copy_from(file=open(filename), sep='|',
                   table=schema + '.tgid', columns=file_columns)
    conn.commit()

    # id | ts | call stack level | OoT | InT | ins count | call (c/r/e/u) | cpu | thread_id | mod | sym
    curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                 '.ins (id serial, ts bigint, level smallint, ts_oot bigint, ts_int bigint, ins_count int, ' +
                 'call varchar(1), cpu smallint, thread_id int, module_id smallint, symbol_id int ) ' +
                 'with (fillfactor=100)')
    filename = fn + '.sat0'

    file_columns = ('ts', 'level', 'ts_oot', 'ts_int', 'ins_count', 'call', 'cpu', 'thread_id', 'module_id',
                    'symbol_id')

    curs.copy_from(file=open(filename), sep='|', table=schema + '.ins', columns=file_columns)

    conn.commit()

    filename = fn + '.satmod'
    separator = get_separator(filename)

    curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                 '.module (id serial, module varchar(1024), PRIMARY KEY(id)) with (fillfactor=100)')
    filename = fn + '.satmod'
    file_columns = ('id', 'module')
    curs.copy_from(file=open(filename), sep=separator,
                   table=schema + '.module', columns=file_columns)
    conn.commit()

    # for backward compatibility to be removed later
    filename = fn + '.satsym'
    separator = get_separator(filename)

    # Find longest symbol name
    sep_cnt = 0
    longest_sym = 0
    longest_fullsym = 0
    with open(filename, 'r') as inF:
        for line in inF:
            if sep_cnt == 0:
                sep_cnt = line.count(separator)
            if sep_cnt == 1:
                id_, sym_ = line.split(separator)
                if longest_sym < len(sym_):
                    longest_sym = len(sym_)
            else:
                id_, sym_, fsym_ = line.split(separator)
                if longest_sym < len(sym_):
                    longest_sym = len(sym_)
                if longest_fullsym < len(fsym_):
                    longest_fullsym = len(fsym_)

    file_colums = None
    if sep_cnt == 1:
        curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                     '.symbol (id serial, symbol varchar(' + str(longest_sym) + '), PRIMARY KEY(id)) with (fillfactor=100)')
        file_columns = ('id', 'symbol')
    else:
        curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                     '.symbol (id serial, symbol varchar(' + str(longest_sym) + '), fullsymbol varchar(' +
                     str(longest_fullsym) + '), PRIMARY KEY(id)) with (fillfactor=100)')
        file_columns = ('id', 'symbol', 'fullsymbol')
    filename = fn + '.satsym'
    curs.copy_from(file=open(filename), sep=separator,
                   table=schema + '.symbol', columns=file_columns)
    conn.commit()

    curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                 '.cbr (ts bigint, cpu smallint, acbr smallint, ecbr smallint, PRIMARY KEY(ts, cpu)) ' +
                 'with (fillfactor=100)')
    filename = fn + '.satcbr'
    file_columns = ('ts', 'cpu', 'acbr', 'ecbr')
    curs.copy_from(file=open(filename), sep='|',
                   table=schema + '.cbr', columns=file_columns)
    conn.commit()

    # Import extra trace info to db
    if os.path.isfile(fn + '.satstats'):
        curs.execute('CREATE TABLE IF NOT EXISTS ' + schema +
                     '.info (key varchar(256), value bigint, info varchar(2048), PRIMARY KEY(key))')
        filename = fn + '.satstats'
        file_columns = ('key', 'value', 'info')
        curs.copy_from(file=open(filename), sep='|',
                       table=schema + '.info', columns=file_columns)
        conn.commit()

    # Import screen shot from the device to DB
    if os.path.isfile(fpath + '/screen.png'):
        f = open(fpath + '/screen.png', 'rb')
        filedata = psycopg2.Binary(f.read())
        f.close()
        curs.execute("INSERT INTO public.screenshots(id, file_data) VALUES (%s,%s)", (insert_id, filedata, ))
        conn.commit()
        curs.execute("""UPDATE public.traces SET screenshot = TRUE WHERE id = %s;""", (insert_id, ))
        conn.commit()

    # Calculate global CPU count
    curs.execute("""select max(cpu) from """ + schema + """.ins""")
    conn.commit()
    cpu_count = curs.fetchone()[0] + 1

    curs.execute("""UPDATE public.traces SET cpu_count = %s WHERE id = %s;""", (cpu_count, insert_id))
    conn.commit()

    return


def bugFixHack1(schema):
    curs.execute('DELETE FROM ' + schema + '.ins WHERE ts > 2147483646;')
    return


def createIndexs(schema):
    curs.execute('ALTER TABLE ' + schema + '.ins ADD CONSTRAINT id_pk PRIMARY KEY(id);')
    curs.execute('CREATE INDEX ts_idx ON ' + schema + '.ins USING btree (ts) with (fillfactor=100);')
    conn.commit()
    curs.execute('CREATE INDEX ins_idx ON ' + schema +
                 '.ins USING btree (thread_id, level, ts, module_id, symbol_id ) with (fillfactor=100);')

    conn.commit()


def RGBToHTMLColor(rgb_tuple):
    """ convert an (R, G, B) tuple to #RRGGBB """
    hexcolor = '#%02x%02x%02x' % rgb_tuple
    return hexcolor


def createColors(schema):
    # Create field for the HTML-color
    # named_cur.execute("""SELECT * FROM """ + schema + """.tgid GROUP BY pid,tgid ORDER by pid,tgid""")

    # Get process and task id's
    named_cur.execute("""select pid, array_agg(id) as ids, array_agg(tgid) as tgids, array_agg(name) name_arr
    from ( select * from """ + schema + """.tgid order by pid, tgid ) as s1
    group by pid order by pid""")
    rows = named_cur.fetchall()

    # Calculate the colors for processes
    # Every process should have own color
    # Every thead in same process should same color, but different lightning
    processMaxCount = named_cur.rowcount
    processCounter = 0
    for row in rows:
        x = (1.0 / processMaxCount) * processCounter
        processCounter += 1
        threadCount = len(row.tgids)
        for tc in range(0, threadCount):
            y = 0.0 + (0.4 / threadCount) * tc
            y = 1.0 - y - (0.05 / threadCount)
            z = 200 + (40 / threadCount) + (50 / threadCount * tc)
            # Push color back to DB
            curs.execute("""UPDATE """ + schema +
                         """.tgid SET color = (%s) WHERE id = (%s);""",
                         (RGBToHTMLColor(colorsys.hsv_to_rgb(x, y, z)), row.ids[tc],))


def helperCreateAvgGraphTable(schema, cpux):
    if cpux == 0:
        sql_action = """create table """ + schema + """.graph as """
    else:
        sql_action = """insert into """ + schema + """.graph """

    print "cpu =", cpux
    curs.execute(sql_action + """
    select * from (
    select gen_ts, thread_id, sum(part2)::int, cpu, count(*) over (partition by gen_ts/7980) as ts_count
    from
        (
        select * ,
        -- FINAL CALCULATION IS DONE HERE!!!!
            CASE WHEN row_number() over(partition by id ) = 1 AND (gen_ts + 7980) < (end_ts) THEN
            -- THIS NEED Percent calcutation to figure out exec size
                -- ( Amount * Multiplier ) / slice
                --Multiplier
                (ins_count * (7980 - ( great - gen_ts))::bigint) / (len + 1)::real

            ELSE
            -- Check if we are in row 2 or so
                -- ( Amount * Multiplier ) / slice
                -- OPTION 1 or
                --(ins_count * CASE WHEN end_ts - great + 1 >= 7980 THEN 7980 ELSE end_ts - great + 1 END) / (len + 1)
                -- OPTION 2 check witch one is faster
                CASE WHEN row_number() over(partition by id ) = 1 THEN
                    ins_count::real
                ELSE
                    (ins_count * CASE WHEN end_ts - great + 1 >= 7980 THEN 7980 ELSE end_ts - great + 1 END::bigint) /
                    (len + 1)::real
                END
            END
            as part2

         from
        (
        select *, greatest(ts, gen_ts) as great from
        (
        select  end_ts - ts as len,
            *,
            generate_series(ts - (ts % 7980), end_ts, 7980) as gen_ts
        from
        (
        --SELECT lead(ts) over (order by id) -1 as end_ts ,*
        SELECT lead(ts) over () -1 as end_ts ,*
        FROM """ + schema + """.ins
        WHERE cpu = """ + str(cpux) + """
        ORDER BY ID
        ) s2
        ) s3
        ) s4
        ) s5
        group by 1,2, cpu
        order by gen_ts ) s6
    where ts_count = 1 and sum <> 0;""")
    conn.commit()


# This will create average Graph table which will help querying graph out of big ins traces
# We will use 1us resultion which is now 1596 tics
def createAvgGraphTable(schema):
    for x in range(0, cpu_count):
        helperCreateAvgGraphTable(schema, x)


def getTscTick(schema):
    # Backward compatibility with old traces
    data = "1330000"
    # Backward compatibility check, if info table exists
    curs.execute("select exists(select * from information_schema.tables where table_name=%s and table_schema=%s)",
                 ('info', schema,))
    res = curs.fetchone()
    if res[0]:
        curs.execute("SELECT value from " + schema + ".info WHERE key = 'TSC_TICK'")
        data = curs.fetchone()[0]
    return int(data)


# Find out different infos
def digTraceInfo(schema, id, trace_path):
    curs.execute("select max(ts) from " + schema + ".ins")
    ts = curs.fetchone()[0]
    tsc_tick = getTscTick(schema)

    # Lenght in ms
    length = int(round(ts / tsc_tick))
    curs.execute("UPDATE public.traces SET length = %s WHERE id = %s;", (length, id))
    conn.commit()

    # get Build info
    build_info = getTraceBuildInfo(trace_path)
    if build_info:
        curs.execute("UPDATE public.traces SET build = %s, device=%s WHERE id = %s;",
                     (build_info['version'] + '/' + build_info['type'], build_info['name'] + '/' +
                      build_info['platform'], id))
        conn.commit()


#  Load Trace Build info from the file
def getTraceBuildInfo(trace_path):
    if os.path.exists(os.path.join(trace_path, "build_info.p")):
        build_info = pickle.load(open(os.path.join(trace_path, "build_info.p"), "rb"))
    else:
        build_info = None
    return build_info


def main():
    global insert_id
    #
    #    Argument parsing and menu
    #
    parser = argparse.ArgumentParser(version='1.0', description='SAT db trace importer', add_help=True)

    parser.add_argument('-t', action='store', dest='device',
                        help='Device under tracing?', default='VLV')
    parser.add_argument('-d', action='store', dest='description',
                        help='Explain, what have you traced?', default='Explain, what have you traced?')
    parser.add_argument('-i', action='store', dest='traceid',
                        help='TraceID?', default=False)
    parser.add_argument('trace_path', action="store",
                        help='Trace folder path')

    results = parser.parse_args()
    print 'path_value       =', results.trace_path

    if os.path.isabs(results.trace_path):
        TRACE_FOLDER_PATH = os.path.realpath(results.trace_path + '/..')
        results.trace_path = os.path.basename(os.path.normpath(results.trace_path))
        os.chdir(TRACE_FOLDER_PATH)

    # Look for sat0 file
    files = glob.glob('./' + results.trace_path + '/*.sat0')

    if not len(files):
        print "\n Can't find SAT-files from ./" + results.trace_path + '/ folder'
        print ""
        parser.print_help()
        return

    trace_name = files[0][:-5]
    trace_path = results.trace_path

    #
    #    Open Task switch file handles
    #
    cpuf = {}

    #
    #    Create trace metadata row to DB
    #     - id will be schema name 't' + id
    #
    try:
        status.createTracesTable()

        curs.execute('CREATE TABLE IF NOT EXISTS public.screenshots (id int, file_data bytea not null , PRIMARY KEY(id))')
        conn.commit()

        status.rollup_db_schema()

        if not results.traceid:
            curs.execute("INSERT INTO public.traces (name, cpu_count, device, created) " +
                         "values (%s, %s, %s, now()) RETURNING id",
                         (results.trace_path, len(cpuf), results.device))
            conn.commit()
            insert_id = curs.fetchone()[0]
        else:
            curs.execute("UPDATE public.traces SET name = %s, cpu_count = %s, device = %s " +
                         "WHERE id = %s;",
                         (results.trace_path, len(cpuf), results.device, results.traceid))
            conn.commit()
            insert_id = results.traceid

        status.update_status(insert_id, status.IMPORT)

        schema = 't' + str(insert_id)
        curs.execute('DROP SCHEMA IF EXISTS ' + schema + ';')
        curs.execute('CREATE SCHEMA ' + schema + ';')

        print "*************************************\n"
        print "Import Data"
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        importCSV(insert_id, trace_name, trace_path)
        print "*************************************\n"
        print "create Indexes"
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        createIndexs(schema)
        print "*************************************\n"
        print "Create colors for prosesses and threads"
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        createColors(schema)
        print "*************************************\n"
        print "Calculate avg graph table"
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        createAvgGraphTable(schema)
        print "*************************************\n"
        print "Dig some trace info for the trace"
        digTraceInfo(schema, insert_id, results.trace_path)
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        print "*************************************\n"
        print "All Done"
        print strftime("%Y-%m-%d %H:%M:%S", gmtime())
        status.update_status(insert_id, status.READY, results.description)
    except Exception as e:
        print "Import Failed: " + str(e)
        status.update_status(insert_id, status.FAILED, 'Importing to DB Failed!')
        sys.exit(1)

    return

if __name__ == "__main__":
    main()
    # All ok
    sys.exit(0)
