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
    .controller('MenuCtrl', MenuCtrl);

  MenuCtrl.$inject = ['$scope', '$rootScope', '$resource', '$location', '$routeParams', 'menuService', 'traceInfo'];

  function MenuCtrl($scope, $rootScope, $resource, $location, $routeParams, menuService, traceInfo) {

    $rootScope.traceName = '';

    //URI changed, check if we need to change a route too
    $scope.$on('$routeChangeSuccess', function (/*event*/) {
      //TODO check if path exists
      //If not add to path
      if ($routeParams.traceID)
      {
        var tinfo = traceInfo.getData();
        menuService.addItem({ 'id': $routeParams.traceID, 'active' : 1, 'name' : 'Trace ' + $routeParams.traceID + ' - ' + tinfo.trace.name, 'href' : '#/trace/' + $routeParams.traceID });
      }
      else
      {
        menuService.setActive(0);
      }
    });

    //Set the scope to trace menulist in menu service
    $scope.menu = menuService.menu;

    // console.log('trace_id='+$routeParams.traceID);
    // console.log("path="+$location.path());

    $scope.showinfo = function () {
      if (!$routeParams.traceID) { return; }

      var id = $routeParams.traceID;

      $scope.traceInfo = null;
      $scope.traceId = id;

      var tiPromise = traceInfo.get(id);
      tiPromise.then(function(resp) {
        $scope.traceInfo = resp.data.infos;
        $scope.selectTrace = resp.data.trace;
        if (resp.data.trace.screenshot)
        {
          $scope.showScreenShot = true;
        }
      });
      $scope.infoVisible = true;
      $scope.infoy = { top: '80px' };
    };

    $scope.hideinfo = function(/*id*/) {
      $scope.infoVisible = false;
      $scope.selectTrace = false;
      $scope.showScreenShot = false;
    };
  }
})();
