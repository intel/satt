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
    .controller('AdminCtrl', AdminCtrl);

  AdminCtrl.$inject = ['$scope', '$rootScope', '$resource', '$location', 'menuService', 'Flash'];

  function AdminCtrl($scope, $rootScope, $resource, $location, menuService, flash) {
    var blockNext = false;
    $rootScope.traceName = ' - Admin Traces';
    var Traces = $resource('/api/1/traces/');
    var traces = Traces.query(function () {
      $scope.traces = traces;
    });

    $scope.click = function (index, id) {
      if (blockNext)
      {
        blockNext = false;
        $location.path('/admin');
        return;
      }
      $location.path('/admin/' + id);
    };

    $scope.delete = function (id, index) {
      blockNext = true;
      var r = confirm('Are you sure to Delete trace ' + id);
      if (r === true) {
        var Trace = $resource('/api/1/trace/:traceId', { traceId : id });
        Trace.delete();
        $scope.traces.splice(index, 1);
        flash.showInfo('Trace "' + id + '" deleted succesfully.');
      }
    };
  }
})();
