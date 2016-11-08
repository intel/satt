/*
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
*/

(function () {

  'use strict';

  angular
    .module('satt')
    .factory('insflowService', insflowService);

  function insflowService() {
    var ts = 21;
    var getTs = function(start)
    {
      ts = ts + 1;
      return ts + start;
    };
    // Public API here
    return {
      getData: function(pid,start) {
        return {data:[
          {'id':1,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123010, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 1 Symbold Name'},
          {'id':2,'l':0,'cl':'c', 'ts': getTs(start), 'of': 123020, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 2 Symbold Name'},
          {'id':3,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123030, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 3 Symbold Name'},
          {'id':4,'l':0,'cl':'c', 'ts': getTs(start), 'of': 123040, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 4 Symbold Name'},
          {'id':5,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123050, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 5 Symbold Name'},
          {'id':6,'l':0,'cl':'c', 'ts': getTs(start), 'of': 123060, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 6 Symbold Name'},
          {'id':7,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123070, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 7 Symbold Name'},
          {'id':8,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123080, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 8 Symbold Name'}
        ]};
      },
      getNode: function(nodeid,level,start) {
        return [
          {'id':3,'l':level, 'cl':'e', 'ts': getTs(start), 'of': 123011, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Sub Call 1 Symbold Name'},
          {'id':4,'l':level, 'cl':'c', 'ts': getTs(start), 'of': 123012, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Sub Call 2 Symbold Name'},
          {'id':5,'l':level, 'cl':'e', 'ts': getTs(start), 'of': 123013, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Sub Call 3 Symbold Name'},
          {'id':6,'l':level, 'cl':'c', 'ts': getTs(start), 'of': 123014, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Sub Call 4 Symbold Name'},
          {'id':7,'l':level, 'cl':'e', 'ts': getTs(start), 'of': 123013, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Sub Call 5 Symbold Name'},
        ];
      },
      getSymbol: function(/*symbolId*/) {
        return {};
      }
    };
  }
})();

/*
id (db) | ts | oot | int | ins count | call (c/r/e/u) | cpu | thread_id | mod | sym
------------------------------------------------------------------------------------
ts | call stack level | OoT | InT | ins count | call (c/r/e/u) | cpu | thread_id | mod | sym
1 c
1 c
2 c
3 c
4 r
5 r
6 r
7 r

{'id':1,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123010, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 1 Symbold Name'},
{'id':2,'l':0,'cl':'c', 'ts': getTs(start), 'of': 123020, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 2 Symbold Name'},
{'id':3,'l':0,'cl':'e', 'ts': getTs(start), 'of': 123030, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 3 Symbold Name'},
{'id':4,'l':0,'cl':'c', 'ts': getTs(start), 'of': 123040, 'it': 12313, 'in': 12331, 'mod': 'some_object.so', 'sym': 'Some 4 Symbold Name'},
*/
