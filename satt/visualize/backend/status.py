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
import psycopg2
import psycopg2.extras
import ConfigParser

SAT_HOME = os.environ.get('SAT_HOME')
CONFIG_FILE = os.path.join(SAT_HOME, 'conf', 'db_config')

#Singleton
ins = None
def getStatus():
    global ins
    if not ins:
        ins = Status()
    return ins

class Status(object):
    def __init__(self):
        self.dbconfig = {}
        self._initConfig()

        self.data = []
        self.conn = psycopg2.connect(dbname=self.dbconfig['dbname'],
            user=self.dbconfig['user'], password=self.dbconfig['password'])
        self.cursor = self.conn.cursor()
        self.named_cursor = self.conn.cursor(cursor_factory=psycopg2.extras.NamedTupleCursor)
        self.FAILED = 100
        self.QUEUE = 40
        self.UNZIP = 30
        self.PROCESS = 20
        self.IMPORT = 10
        self.READY = 0

    def getDbConfig(self, key):
        if not key in self.dbconfig:
            return None
        return self.dbconfig[key]

    def _printInitConfigHelp(self):
        print "ERROR!"
        print "<satt>/conf/db_config file is needed"
        print ""
        print "[DB]"
        print "dbname: <dbname>"
        print "user: <username>"
        print "password: <password>"

    def _initConfig(self):
        self.config = ConfigParser.ConfigParser()
        self.config.read(CONFIG_FILE)
        error = False
        try:
            self.dbconfig['dbname'] = self.config.get('DB', 'dbname')
            self.dbconfig['user'] = self.config.get('DB', 'user')
            self.dbconfig['password'] = self.config.get('DB', 'password')
        except Exception as e:
            error = True
        finally:
            if error:
                self._printInitConfigHelp()
                raise Exception('<satt>/conf/db_config was not found or it is missing field(s)')

    def createTracesTable(self):
        self.cursor.execute('CREATE TABLE IF NOT EXISTS public.traces (id serial, name varchar(256), description varchar(2048),' +
                     'created date, device varchar(2048), cpu_count int4, length int DEFAULT 0, ' +
                     "build varchar(2048) DEFAULT '', contact varchar(2048) DEFAULT '', " +
                     "screenshot boolean DEFAULT 'false', " +
                     "status smallint default 0, info varchar(2048) DEFAULT '', PRIMARY KEY(id))")
        self.conn.commit()

    def create_id(self, trace_file_name, filename):
        self.createTracesTable()

        self.rollup_db_schema()

        # status:
        # 40 - queuing
        # 30 - unzipping
        # 20 - processing
        # 10 - importing
        # 0  - Ready
        self.cursor.execute("INSERT INTO public.traces " +
                            "(name, description, cpu_count, device, created, status, info)" +
                            "values (%s, %s, %s, %s, now(), %s, %s) RETURNING id",
                            (filename, 'Queueing', 0, '-', self.QUEUE, trace_file_name, ))
        self.conn.commit()
        insert_id = self.cursor.fetchone()[0]

        return insert_id

    def get_file_by_id(self, trace_id):
        self.cursor.execute("SELECT info from public.traces where id=%s", (trace_id,))
        res = self.cursor.fetchone()
        if res[0]:
            return res[0]
        raise Exception('Filename was not found with this id')

    def update_status(self, trace_id, new_status, description=False):
        if new_status == self.UNZIP:
            description = "Unzipping"
        elif new_status == self.PROCESS:
            description = "Prosessing"
        elif new_status == self.IMPORT:
            description = "Importing trace to DB"

        if not description:
            self.cursor.execute("""UPDATE public.traces SET status = %s WHERE id = %s;""",
                                (new_status, trace_id))
        else:
            self.cursor.execute("""UPDATE public.traces SET description = %s, status = %s WHERE id = %s;""",
                                (description, new_status, trace_id))

        self.conn.commit()
        return

    def _getTscTick(self, schema):
        # Backward compatibility with old traces
        data = "1330000"
        # Backward compatibility check, if info table exists
        self.cursor.execute("select exists(select * from information_schema.tables where table_name=%s " +
                            "and table_schema=%s)", ('info', schema,))
        res = self.cursor.fetchone()
        if res[0]:
            self.cursor.execute("SELECT value from " + schema + ".info WHERE key = 'TSC_TICK'")
            data = self.cursor.fetchone()[0]
        return int(data)

    # Find out different infos
    def digAllTraceInfo(self):
        self.cursor.execute("select id from public.traces")
        traces = self.cursor.fetchall()
        for t in traces:
            schema = 't' + str(t[0])
            self.cursor.execute("SELECT EXISTS(SELECT * FROM information_schema.tables WHERE table_schema = %s AND " +
                                "table_name = 'ins');", (schema, ))

            ns = self.cursor.fetchone()[0]
            if ns:
                self.cursor.execute("select ts from " + schema + ".ins order by id desc limit 1;")
                ts = self.cursor.fetchone()[0]
                tsc_tick = self._getTscTick(schema)

                # Lenght in ms
                length = int(round(ts / tsc_tick))

                self.cursor.execute("UPDATE public.traces SET length = %s WHERE id = %s;", (length, t[0],))
                self.conn.commit()

    # Rollup db migrations to latest and greatest
    def rollup_db_schema(self):
        if not self.check_if_column_exists('public', 'traces', 'status'):
            self.create_column('traces', 'status', 'smallint', 0)
            self.create_column('traces', 'info', 'varchar(2048)', '')

        if not self.check_if_column_exists('public', 'traces', 'length'):
            self.create_column('traces', 'length', 'int', 0)
            self.remove_column('traces', 'start_time')
            self.remove_column('traces', 'end_time')
            self.digAllTraceInfo()

        if not self.check_if_column_exists('public', 'traces', 'build'):
            self.create_column('traces', 'build', 'varchar(2048)', '')

        if not self.check_if_column_exists('public', 'traces', 'contact'):
            self.create_column('traces', 'contact', 'varchar(2048)', '')

        if not self.check_if_column_exists('public', 'traces', 'screenshot'):
            self.create_column('traces', 'screenshot', 'boolean', 'false')

    # Helper function to check if column exists in table
    def check_if_column_exists(self, schema, table, column):
        print "Check if column exists column {0} {1} {2}".format(schema, table, column)
        self.cursor.execute("select exists(select * from information_schema.columns where table_schema=%s " +
                            "and table_name=%s and column_name=%s);", (schema, table, column,))
        res = self.cursor.fetchone()
        return res[0]

    def create_column(self, table, column, type, default=False):
        print "Create column {0} {1}".format(table, column)
        if default:
            self.cursor.execute("ALTER TABLE " + table + " ADD COLUMN " + column + " " + type + " DEFAULT " + default +
                                ";")
        else:
            self.cursor.execute("ALTER TABLE " + table + " ADD COLUMN " + column + " " + type + ";")
        self.conn.commit()
        return

    def remove_column(self, table, column):
        self.cursor.execute("ALTER TABLE " + table + " DROP COLUMN IF EXISTS " + column + ";")
        self.conn.commit()
        return

if __name__ == '__main__':
    status = Status()
    status.rollup_db_schema()
