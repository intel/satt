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

  /**
   * Service to provide basic trace info to trace views
   */

  angular
    .module('satt')
    .factory('traceInfo', traceInfo);

  traceInfo.$inject = ['$http'];

  function traceInfo($http) {
    var promise;
    var data;
    var cache = [];
    var dataCache = [];
    var getTraceInfo = function (id) {
      if (!cache[id]) {

        var url = '/api/1/traceinfo/' + id;
        // $http returns a promise, which has a then function, which also returns a promise
        promise = $http.get(url).then(function (response) {
          data = response.data;
          _.forEach(data.infos, function(item) {
            data[item.key] = item.value;
          });
          data.infos.unshift({key : 'Desc', value : '', info : data.trace.description});
          data.infos.unshift({key : 'Build', value : '', info : data.trace.build});
          data.infos.unshift({key : 'Length', value : data.trace.length + ' ms', info : 'Trace length in ms'});
          data.infos.unshift({key : 'Name', value : data.trace.name, info : 'Trace name'});
          dataCache[id] = data;
          return response;
        });
        cache[id] = promise;
        return promise;
      }
      else {
        data = dataCache[id];
        return cache[id];
      }
    };

    var traceInfoService = {
      getData: function () {
        return data;
      },
      tscTicksToNs: function (tsc) {
        return (tsc) / (parseInt(data.TSC_TICK, 10) / 1000000);
      },
      get: function (id) {
        // Return the promise to the controller
        return getTraceInfo(id);
      }
    };
    // Public API
    return traceInfoService;
  }
})();
