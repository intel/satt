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
    .controller('SearchCtrl', SearchCtrl);
  SearchCtrl.$inject = ['$scope', '$http', '$routeParams', '$timeout', '$rootScope', 'broadcastService'];

  function SearchCtrl($scope, $http, $routeParams, $timeout, $rootScope, broadcastService) {

    /* Init global configs */
    var globalConfig =  { fetchingSymbol: -1, };
    $rootScope.globalConfig = globalConfig;

    var data = { visible : false,
                 search : '',
                 results : '',
                 error : false,
                 errorMsg : '',
                 searchVisible : false,
                 searching : false,
               };
    $scope.data = data;

    var timeoutId = null;


    $scope.searchkey = function () {
      if ($scope.data.search.length && $scope.data.search.length > 3)
      {
      /* TODO we could do some more clever search here */
      }
    };

    /**
    * Search called
    */
    $scope.search = function() {
      /* TODO can we handle other button some other way??? */
      if ($scope.data.searchVisible)
      {
        $scope.data.searchVisible = false;
        broadcastService.bcClearSearchHits();
        return;
      }

      $scope.data.visible = true;
      var searchUrl = '/api/1/search/' + $routeParams.traceID;
      var searchCountUrl = '/api/1/search/hits/' + $routeParams.traceID;

      /* fetch Hit couts for the Search results later */
      var fetchHitCounts = function () {
        var ids = [];
        for (var x in data.results)
        {
          ids.push(data.results[x].id);
        }

        $http({ method: 'POST',
          url: searchCountUrl,
          data: { 'ids' : ids }
        }).
        success(function(respdata /*, status, headers, config*/) {
          if (respdata.data.length)
          {
            for (var x in respdata.data)
            {
              for (var y in data.results)
              {
                if (respdata.data[x].id === data.results[y].id)
                {
                  data.results[y].hits = respdata.data[x].hits;
                  break;
                }
              }
            }
          }
        }).
        error(function(/* data, status, headers, config*/) {
        });
      };

      /**
      * Get data for your node
      */
      $scope.data.searching = true;
      $http({ method: 'POST',
              url: searchUrl,
              data: { 'search' : $scope.data.search }
          }).
          success(function(respdata /*, status, headers, config*/) {
            if (respdata.data.length)
            {
              data.results = respdata.data;
              timeoutId = $timeout(function() {
                fetchHitCounts();
              },10);
            }
            else
            {
              data.results = [{'id':0xffffffff,'symbol':'No symbols found!'}];
            }
            //insertArrayAt(scope.calls, ++index, respdata.data);
            $scope.data.searching = false;
          }).
          error(function(/* data, status, headers, config*/) {
            // called asynchronously if an error occurs
            // or server returns response with status
            // code outside of the <200, 400) range
            $scope.data.error = true;
            $scope.data.errorMsg = 'ERROR';
            $scope.data.searching = false;
          }
        );
    };

    $scope.getSymbol = function(symbolId /*,index*/) {
      //If auto hide search window when clicked function
      if ( $scope.data.searchVisible )
      {
        broadcastService.bcClearSearchHits();
      }

      $rootScope.globalConfig.fetchingSymbol = symbolId;
      $scope.data.searchVisible = true;
      broadcastService.bcSearchHits(symbolId);
    };

    $scope.clearSearch = function() {
      $scope.data.searchVisible = false;
      broadcastService.bcClearSearchHits();
    };
  }
})();
