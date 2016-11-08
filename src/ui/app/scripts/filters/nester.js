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
/**
* Will character beginning of symbols to illustrace nested calls
*
* --> Symbol1()
* ---> Symbol2()
* TODO to be removed, not needed anymore
*/

(function () {

  'use strict';

  angular
    .module('satt')
    .filter('nester', nester);

  nester.$inject = [ 'traceInfo'];

  function nester(traceInfoService) {
    return function (input) {
      var tscTickToMsMultiplier = 1 / (parseInt(traceInfoService.getData().TSC_TICK, 10) / 1000000);
      var ns = Math.round(input * tscTickToMsMultiplier);
      if (ns / 1000000 > 1)
      {
        return  Math.round(ns / 1000) / 1000 + ' ms';
      }
      return  ns + ' ns';
    };
  }
})();
