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
    .factory('processInfo', processInfo);

  function processInfo() {
    var processes = [];
    var service = {
      addItem: function (item) {
        if (_.findIndex(processes, { 'tgid' : item.tgid }) === -1)
        {
          processes.push({'tgid' : item.tgid, 'pn' : item.pn});
        }
        return;
      },
      getProcessNameByTgid: function (tgid) {
        var index = -1;
        index = _.findIndex(processes, { 'tgid' : tgid });
        if (index !== -1) {
          return processes[index].pn;
        }
        else {
          return 'unknown';
        }
      },
      clearAll: function () {
        processes = [];
        return ;
      }
    };
    return service;
  }
})();
