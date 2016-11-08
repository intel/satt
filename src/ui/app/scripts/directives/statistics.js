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
    .directive('statistics', statistics);

  statistics.$inject = ['$routeParams','$http','$rootScope','broadcastService','bookmark'];

  function statistics($routeParams, $http, $rootScope, bcService,bookmarkService) {
    return {
      templateUrl : 'views/statistics.html',
      restrict: 'E',
      scope: true,
  /*      scope: {
        pname: '@',
        pid: '@',
        start: '@',
        end: '@',
      },*/
      link: function staticLink(scope, element, attrs) {
        scope.groupings = [{'type':'tgid','name':'Group by Processes','url':'process/', templateName : 'views/statistics/process_template.html'},
                           {'type':'pid','name':'Group by Threads','url':'thread/', templateName : 'views/statistics/thread_template.html'},
                           {'type':'module_id','name':'Group by Module','url':'module/', templateName : 'views/statistics/module_template.html'}];

        /**
         * Add view to bookmarks
         */
        bookmarkService.addStatistics(scope.$id,attrs.start,attrs.end);

        scope.$on('$destroy', function() {
          bookmarkService.removeStatistics(scope.$id);
        });


        scope.attrs = attrs;
        var group, groups = [];
        var items = [];
        items.tgid = [], items.pid = [], items.module_id = [];
        scope.grouping = scope.groupings[0];
        scope.group = group;
        scope.groups = '';
        scope.groupTemplate = 'views/statistics/process_template.html';
        scope.items = '';

        /* Column title for function calls */
        scope.head = [
            {'col':'symbol','sym': 'Symbol name'},
            {'col':'call_count','sym': 'Calls'},
            {'col':'ins','sym': 'ins total'},
            {'col':'ins_per_call','sym': 'Avg ins / call'},
            {'col':'in_thread','sym': 'In thread total (cum)'},
            {'col':'avg_in_thread','sym': 'In thread Avg / call (cum)'},
            {'col':'in_abs_thread_per_call','sym': 'In thread Avg / call (abs)'},
            {'col':'min_in_thread','sym': 'In thread Min (cum)'},
            {'col':'max_in_thread','sym': 'In thread Max (cum)'},
            {'col':'out_thread','sym': 'Out of thread total (cum)'}
          ];

        /* Sorting columns */
        scope.sort = {
          //column: 'ins',
          column: 'ins',
          descending: true
        };

        scope.selectedCls = function(column) {
            return column === scope.sort.column && 'sort-' + scope.sort.descending;
        };

        scope.changeSorting = function(column) {
            var sort = scope.sort;
            if (sort.column === column) {
                sort.descending = !sort.descending;
            } else {
                sort.column = column;
                sort.descending = false;
            }
        };

        /* Highlight graph area where statistics belongs to */
        scope.statsFocus = function() {
          var area = {};
          area.start = attrs.start;
          area.end = attrs.end;
          area.id = -1;
          bcService.bcCodeArea(area);
        };

        scope.statsBlur = function() {
          bcService.bcCodeAreaClear();
        };

        /*
        * TODO Handle item select to show
        *  - callers
        *  - calleers
        *  - histogram
        */
        scope.selectItem = function(id) {
          var callerUrl = '/api/1/statistics/callers/' + $routeParams.traceID+'/'+attrs.start+'/'+attrs.end + '/' + scope.group.tgid + '/' + scope.items[id].symbol_id;
          $http({method: 'GET', url: callerUrl}).
            success(function(/*respdata, status, headers, config*/) {
              //console.log(respdata.data);
              //scope.items = respdata.data;
            }).error(function(/*data, status, headers, config*/) {
              // called asynchronously if an error occurs or server returns response with status
              // code outside of the <200, 400) range
              scope.calls = 'ERROR';
            });
        };

        /*
        * Handle Grouping click from the list
        */
        scope.selectGroup = function(id) {
          if ( scope.group !== scope.groups[id])
          {
            scope.group = scope.groups[id];
          }
        };

        /*
        * Handle Group change
        */
        scope.$watch('group', function(newValue, oldValue) {
          $(element).find('select').blur();
          var statUrl = '';
          /* http request ongoing */
          if ( scope.httpGetStatisticsGroup )  { return; }
          /* React only to new values */
          if ( newValue === oldValue && scope.grouping.type ) { return; }

          scope.items = '';

          /* Check if 100% is selected, then don't fetch functions for performance reasons */
          if (scope.group[scope.grouping.type] === undefined) { return; }

          if (!items[scope.grouping.type][scope.group[scope.grouping.type]])
          {
            switch (scope.grouping.type)
            {
            case 'tgid': /*process*/
              statUrl = '/api/1/statistics/' + scope.grouping.url + $routeParams.traceID+'/'+attrs.start+'/'+attrs.end + '/' + scope.group.tgid;
              break;
            case 'pid': /* thread */
              statUrl = '/api/1/statistics/' + scope.grouping.url + $routeParams.traceID+'/'+attrs.start+'/'+attrs.end + '/' + scope.group.pid;
              break;
            case 'module_id': /* module */
              statUrl = '/api/1/statistics/' + scope.grouping.url + $routeParams.traceID+'/'+attrs.start+'/'+attrs.end + '/' + scope.group.module_id;
              break;
            }

            scope.httpGetStatisticsGroup = true;

            $http({method: 'GET', url: statUrl}).
              success(function(respdata) {
                for(var x=0; x<respdata.data.length; x++)
                {
                  respdata.data[x].ins_per_call = respdata.data[x].ins / respdata.data[x].call_count;
                  respdata.data[x].in_abs_thread_per_call = respdata.data[x].in_abs_thread / respdata.data[x].call_count;
                }
                items[scope.grouping.type][scope.group[scope.grouping.type]] = respdata.data;
                scope.items = items[scope.grouping.type][scope.group[scope.grouping.type]];
                scope.httpGetStatisticsGroup = false;
              }).error(function(/*data, status, headers, config*/) {
                // called asynchronously if an error occurs or server returns response with status
                // code outside of the <200, 400) range
                scope.calls = 'ERROR';
                scope.httpGetStatisticsGroup = false;
              });
          }
          else
          {
            scope.items = items[scope.grouping.type][scope.group[scope.grouping.type]];
          }

        });

        /*
        * Handle Grouping change
        */
        scope.$watch('grouping', function(/*newValue, oldValue*/) {
          if ( ! groups[scope.grouping.type] ) {
            // To show wait icon
            scope.groups = '';
            scope.group = '';
            scope.items = '';
            $(element).find('select').blur();

            var statisticsUrl = '/api/1/statistics/groups/'+ scope.grouping.url +$routeParams.traceID+'/'+attrs.start+'/'+attrs.end;
            $http({method: 'GET', url: statisticsUrl}).
              success(function(respdata /*, status, headers, config*/) {
                groups[scope.grouping.type] = respdata.data;
                scope.groups = groups[scope.grouping.type];
                scope.group = scope.groups[0];
                scope.groupTemplate = scope.grouping.templateName;

                // Scroll to Beginning of the Graph
                var target = $('#statistics-'+scope.$id).offset().top - $rootScope.paddingTop - 10;
                $('body,html').animate({scrollTop: target}, 'slow');

              }).error(function(/*data, status, headers, config*/) {
                // called asynchronously if an error occurs or server returns response with status
                // code outside of the <200, 400) range
                scope.calls = 'ERROR';
              });
          }
          else
          {
            scope.groups = groups[scope.grouping.type];
            scope.group = scope.groups[0];
            scope.groupTemplate = scope.grouping.templateName;
          }
        });
      }
    };
  }
})();
