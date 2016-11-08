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
 * Trace Controller
 * - Launch trace views
 * - Launch bookmark views same views and in same order
 *
 */

(function () {

  'use strict';

  angular
    .module('satt')
    .controller('TraceCtrl', TraceCtrl);

  TraceCtrl.$inject = ['$scope', '$rootScope', '$resource', '$routeParams', '$route', '$location', 'traceInfo', 'bookmark'];

  function TraceCtrl($scope, $rootScope, $resource, $routeParams, $route, $location, traceInfo, bookmarkService) {

    /* Set title */
    var tinfo = traceInfo.getData();
    if (tinfo.trace && tinfo.trace.name) {
      $rootScope.satTitle = 'SAT-' + tinfo.trace.name;
    }

    /**
     * Open default view (no bookmark)
     */
    if ($routeParams.traceID && !$routeParams.bookmarkId) {
      $scope.bookmarkviews = [{ type : 'graph', 'data' : {start : '0', end : '0'}}];
    }

    /**
     * Handle Bookmark view loading!
     */
    $scope.$on('$routeChangeSuccess', function (/*event*/) {
      if ($routeParams.traceID && $routeParams.bookmarkId) {
        bookmarkService.get($routeParams.bookmarkId).success(function (resp) {
          var bookmarks = _.sortBy(JSON.parse(resp.data), 'order');
          $scope.bookmarkviews = bookmarks;
        }).error(function (/*fail*/) {
          // Bookmark was not found route back to /trace/wanted
          $location.path( '/trace/' + $routeParams.traceID);
        });
      }
    });
  }
})();
