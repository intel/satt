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
    .factory('broadcastService', broadcastService);

  broadcastService.$inject = ['$rootScope'];

  function broadcastService($rootScope) {

    var bcservice = {};
    var searchSymbolId = -1;
    /**
     * id
     * start in tsc
     * duration in tsc (including out of thead and in thread times)
     */
    bcservice.bcCodePoint = function(id, start, duration) {
      $rootScope.$broadcast('BcCodePoint', { 'id':id, 'start':start, 'duration':duration });
    };
    bcservice.bcCodeArea = function(area) {
      $rootScope.$broadcast('BcCodeArea', area);
    };
    bcservice.bcCodeAreaClear = function() {
      $rootScope.$broadcast('BcCodeAreaClear');
    };
    bcservice.bcClearAllSelections = function() {
      $rootScope.$broadcast('bcClearAllSelections');
    };
    bcservice.bcSearchHits = function(symbolId) {
      searchSymbolId = symbolId;
      $rootScope.$broadcast('BcSearchHit',symbolId);
    };
    bcservice.bcClearSearchHits = function() {
      searchSymbolId = -1;
      $rootScope.$broadcast('BcClearSearchHit');
    };
    bcservice.bcGetSearchSymbolId = function() {
      return searchSymbolId;
    };

    return bcservice;
  }
})();
