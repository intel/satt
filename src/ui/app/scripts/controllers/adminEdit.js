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
    .controller('AdminEditCtrl', AdminEditCtrl);

  AdminEditCtrl.$inject = ['$scope', '$rootScope', '$resource', '$location', '$routeParams', 'Flash'];

  function AdminEditCtrl($scope, $rootScope, $resource, $location, $routeParams, flash) {
    $rootScope.traceName = ' - Admin Traces';
    var Trace = $resource('/api/1/trace/:traceId', { traceId : $routeParams.traceID });
    var trace = Trace.get(function () {
      $scope.trace = trace;
    });

    $scope.submit = function () {
      var retPromise = trace.$save();
      retPromise.then(function (resp) {
        if (resp.status === 'ok') {
          flash.showInfo('Trace ' + $routeParams.traceID + ' info updated succesfully');
        }
        else {
         flash.showError('Trace ' + $routeParams.traceID + ' info update failed!');
        }
        $location.path('/admin/');
      });
    };
  }
})();
