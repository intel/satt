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
    .controller('MainCtrl', MainCtrl);

  MainCtrl.$inject = ['$scope', '$rootScope', '$resource', '$location', 'menuService', 'traceInfo', '$window', 'Flash'];

  function MainCtrl($scope, $rootScope, $resource, $location, menuService, traceInfo, win, flash) {

    $scope.showScreenShot = false;

    /* Set title */
    $rootScope.satTitle = 'SATT';

    var Traces = $resource('/api/1/traces/');

    var traces = Traces.query(function () {
      $scope.traces = traces;
    });

    $scope.click = function (index, id) {
      if ($scope.traces[index].status != 0) {
        traces = Traces.query(function () {
          $scope.traces = traces;
          if ($scope.traces[index].status != 0) {
            flash.showWarning('Trace is still processing and not ready for viewing!');
          }
          else {
            menuService.addItem({'id': id, 'active': 0, 'name': traces[index].name, 'href': '#/trace/' + id });
            $location.path('trace/' + id);
          }
        });
        return;
      }
      menuService.addItem({ 'id': id, 'active': 0, 'name': traces[index].name, 'href': '#/trace/' + id });
      $location.path('trace/' + id);
    };

    $scope.showinfo = function (id, event, index) {
      $scope.traceinfo = null;
      $scope.selectTrace = $scope.traces[index];
      $scope.traceInfoId = id;
      $scope.showScreenShot = $scope.traces[index].screenshot;

      var tiPromise = traceInfo.get(id);
      tiPromise.then(function (resp) {
        $scope.traceinfo = resp.data.infos;
      });
      $scope.infoVisible = true;
      if ( win.innerHeight - event.clientY < 400) {
        $scope.infoy = { bottom: win.innerHeight - event.clientY };
      }
      else {
        $scope.infoy = { top: event.clientY };
      }
    };

    $scope.hideinfo = function (/*id*/) {
      $scope.infoVisible = false;
      $scope.selectTrace = false;
      $scope.showScreenShot = false;
    };
  }
})();
