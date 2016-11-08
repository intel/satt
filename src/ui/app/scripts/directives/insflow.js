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
    .directive('insflow', insflow);

  insflow.$inject = ['$routeParams','$http', 'graphService','broadcastService','$rootScope','traceInfo','bookmark'];

  function insflow($routeParams, $http, graphService,broadcastService,$rootScope,traceInfoService,bookmarkService) {
    return {
      templateUrl : 'views/insflow.html',
      restrict: 'E',
      priority: 0,
      scope: true,
      link: function postLink(scope, element, attrs) {
        var info = {
          'pname': attrs.pname,
          'pid': attrs.pid,
          'start': attrs.start,
          'end': attrs.end
        };
        scope.info = info;
        scope.fixed = false;
        scope.eRowVisible = false;
        scope.autoOpenFlow = true;

        var minLevel = 0;

        /* This is guaranteed by routeProvider resolve */
        var tscTickToMs = 1/(parseInt(traceInfoService.getData().TSC_TICK,10)/1000000);

        /**
        * Handle Window manager Destroy event
        *  cleanup scope and window
        */
        scope.$on('$destroy', function() {
          bookmarkService.removeGraph(scope.$id);
          if(scope.fixed)
          {
            graphService.removeElement(scope.$id);
          }
          $(element).remove();
        });

        scope.toggleERows = function() {
          scope.eRowVisible = !scope.eRowVisible;
        };

        scope.toggleAutoOpenFlow = function() {
          scope.autoOpenFlow = !scope.autoOpenFlow;
        };

        scope.scrollTo = function(id) {
          window.scroll(0, $('#'+id).position().top - $rootScope.paddingTop - 10 /* just some extra */);
        };

        /**
         * Add insflow to bookmark
         */
        bookmarkService.addInsflow(scope.$id, attrs.id, attrs.tgid, attrs.pid, attrs.pname, attrs.start, attrs.end);

        /**
        * Keep Opened Call Stacks here
        */
        var callStacks = [];
        scope.callStacks = callStacks;

        /**
        callStacks = array of callStacks
        call_stack = array of nodes (not tree)
        cs1 - item 1
            --item 2
            ---item 3
        cs2 -item 1
            --item 2
            ---item 3
        */

        /**
        *  BroadCast handlers
        */
        scope.$on('BcCodePoint', function(event,msg) {
          if (msg.start >0)
          {
            scope.higlightId = msg.id;
          }
        });

        // We want to keep 2 leafs
        var modifyCallStacks = function(index, open) {
          // Special case 1st item
          var level = 0xffffffff;
          var node = [];
          var parentNotFound = true;

          // Check if we have same three already open
          for(var x=0; x<callStacks.length; x++)
          {
            if (callStacks[x][callStacks[x].length-1].id === scope.calls[index].parent)
            {
              callStacks[x].push(scope.calls[index]);
              parentNotFound = false;
            }
          }

          // Not in same three (or path in that three)
          if (parentNotFound)
          {
            do
            {
              if (scope.calls[index].l < level)
              {
                level = scope.calls[index].l;
                node.unshift(scope.calls[index]);
              }
            } while(scope.calls[index--].parent !== null);

            if ( open || node.length > 1 && ! open)
            {
              callStacks.unshift(node);
              if (callStacks.length >= 3 )
              {
                callStacks.pop();
              }
            }
          }
        };

        var removeFromCallStack = function(item) {
          for(var x=0; x<callStacks.length; x++)
          {
            //console.log(callStacks);
          }
          callStacks.push(item);
        };

        /**
        * Mouse enter / leave send broadcast message
        */
        scope.enter = function(ts) {
          var index = _.findIndex(scope.calls, {time:ts});
          broadcastService.bcCodePoint(scope.calls[index].id, scope.calls[index].ts, scope.calls[index].duration_tsc);
        };

        var getScaledCallRowColor = function(duration, maxDuration) {
          var alpha = 0.3 + (duration / maxDuration)*0.7;
          return '{\'background-color\':\'rgba(242, 222, 222, ' + alpha + ')\'}';
        };

        var getNextLevel = function(ts, deep) {

          // Max recursion 10 level deep
          deep = deep || 0;
          if (++deep > 10) { return; }

          var index = _.findIndex(scope.calls, {time:ts, cl:'c'});
          var callStartTime = scope.calls[index].ts;
          var callEndTime = 0;
          var insflowUrl = '';

          scope.calls[index].cl = 'r';

          /**
           *
           * Get More insflow rows
           *
           */
          if( index+1 in scope.calls )
          {
            callEndTime = scope.calls[index+1].ts;
          }

          insflowUrl = '/api/1/insflownode/'+$routeParams.traceID+'/'+attrs.id+'/'+callStartTime+'/'+callEndTime+'/'+(scope.calls[index].l + 1);
          $http({method: 'GET', url: insflowUrl}).
            success(function(respdata/*, status, headers, config*/)
            {
              var maxDuration = 0;
              var durationTotal = 0;
              for(var x=0; x<respdata.data.length; x++)
              {
                respdata.data[x].time = Math.round(traceInfoService.tscTicksToNs(respdata.data[x].ts));
                respdata.data[x].duration_tsc = respdata.data[x].it + respdata.data[x].of;
                respdata.data[x].it = Math.round(respdata.data[x].it * tscTickToMs);
                respdata.data[x].of = Math.round(respdata.data[x].of * tscTickToMs);
                respdata.data[x].duration = respdata.data[x].it + respdata.data[x].of;
                respdata.data[x].tPerIns = Math.round(respdata.data[x].duration / respdata.data[x].in);
                respdata.data[x].parent = scope.calls[index].id;
                respdata.data[x].level = new Array(respdata.data[x].l - minLevel + 1).join('-') + '| ';
                if (respdata.data[x].cl === 'c' ) {
                  durationTotal += respdata.data[x].it;
                  if (respdata.data[x].it>maxDuration) {
                    maxDuration = respdata.data[x].it;
                  }
                }
              }

              var hitCount = 0;
              var newOpenTime;
              for(var z=0; z<respdata.data.length; z++) {
                if(respdata.data[z].cl === 'c')
                {
                  respdata.data[z].bgColor = getScaledCallRowColor(respdata.data[z].it, maxDuration);
                  if ( respdata.data[z].it > parseInt(0.7 * maxDuration)) {
                    hitCount +=1;
                    newOpenTime = respdata.data[z].time;
                  }
                }
              }

              // Update call stack view
              modifyCallStacks(index,true);
              insertArrayAt(scope.calls, ++index, respdata.data);

              // Only 1 big function found open some more!
              if (scope.autoOpenFlow && hitCount===1) {
                getNextLevel(newOpenTime, deep);
              }

            }).error(function(/*data, status, headers, config*/) {
                // called asynchronously if an error occurs
                // or server returns response with status
                // code outside of the <200, 400) range
                  scope.calls = 'ERROR';
                });
        };

        /**
         * Clicking row to
         * - open call row
         * - close opened call row
         * - open more row
         */
        scope.click = function(ts, type)
        {
          var index = _.findIndex(scope.calls, {time:ts, cl:type});
          var rowCount = scope.calls.length;

          var callStartTime = scope.calls[index].ts;
          var callEndTime = 0;
          var insflowUrl = '';

          if (scope.calls[index].cl === 'c')
          {
            /**
            * Get data for your node
            */
            getNextLevel(ts);

          }
          //Close function
          else if (scope.calls[index].cl === 'r')
          {
            var x,count=0,level = 0;
            scope.calls[index].cl = 'c';
            level = scope.calls[index].l;
            for (x=index+1;x<scope.calls.length;x++)
            {
              if ( scope.calls[x].l === level)
              {
                break;
              }
              count++;
            }

            // Update call stack view
            modifyCallStacks(index,false);

            scope.calls.splice(index+1,count);
          }
          /**
           * More function
           */
          else if (scope.calls[index].cl === 'm') {

            if( index+1 in scope.calls )
            {
              callEndTime = scope.calls[index+1].ts;
            }

            /* Check if bottom of the call stack was reached */
            if (scope.calls[index].min_level)
            {
              insflowUrl = '/api/1/insflow/'+$routeParams.traceID+'/'+attrs.id+'/'+callStartTime+'/'+attrs.end;
            }
            else
            {
              insflowUrl = '/api/1/insflownode/'+$routeParams.traceID+'/'+attrs.id+'/'+callStartTime+'/'+callEndTime+'/'+(scope.calls[index].l);
            }

            /**
            * Get More data for your node
            */
            $http({method: 'GET', url: insflowUrl}).
              success(function(respdata/*, status, headers, config*/)
            {
              var maxDuration = 0;

              for(var x=0; x<respdata.data.length; x++)
              {
                respdata.data[x].time = Math.round(traceInfoService.tscTicksToNs(respdata.data[x].ts));
                respdata.data[x].duration_tsc = respdata.data[x].it + respdata.data[x].of;
                respdata.data[x].it = Math.round(respdata.data[x].it * tscTickToMs);
                respdata.data[x].of = Math.round(respdata.data[x].of * tscTickToMs);
                respdata.data[x].duration = respdata.data[x].it + respdata.data[x].of;
                respdata.data[x].tPerIns = Math.round(respdata.data[x].duration / respdata.data[x].in);
                respdata.data[x].parent = scope.calls[index].id;
                respdata.data[x].level = new Array(respdata.data[x].l - minLevel + 1).join('-') + '| ';
                if (respdata.data[x].it>maxDuration) {
                  maxDuration = respdata.data[x].it;
                }
              }
              for(var y=0; y<respdata.data.length; y++) {
                if(respdata.data[y].cl === 'c')
                {
                  respdata.data[y].bgColor = getScaledCallRowColor(respdata.data[y].it, maxDuration);
                }
              }
              console.log("scope.calls[i-1]: " + scope.calls[index-1].ts + " " + scope.calls[index-1].l);
              //console.log("scope.calls[i]: " + scope.calls[index].ts + " " + scope.calls[index].l);

              /* Step previous c/e-line from m-line and remove both last c/e-line and m-line.
                  After that insert the next chunk of lines in the end. The removed c/e-line is
                  included in the beginning of the new chunk.
              */
              scope.calls.splice(--index,2);
              insertArrayAt(scope.calls, index, respdata.data);

            }).error(function(/*data, status, headers, config*/) {
                // called asynchronously if an error occurs
                // or server returns response with status
                // code outside of the <200, 400) range
                  scope.calls = 'ERROR';
                });
          }

          if (scope.calls.length !== rowCount && scope.fixed) {
            //TODO height calculation is tricky as DOM is not updated yet
            /*var height = */
            graphService.changeElementHeight(scope.$id, $(element).innerHeight());
          }
        };

        /**
         *
         * Get Initial insflow listing
         *
         */
        var insflowUrl = '/api/1/insflow/'+$routeParams.traceID+'/'+attrs.id+'/'+attrs.start+'/'+attrs.end;
        $http({method: 'GET', url: insflowUrl})
          .success(function(respdata/*, status, headers, config*/)
        {
          /**
          * Fix some data as we getting a bit different results
          */
          var maxDuration = 0;
          minLevel = respdata.min_level;

          for(var x=0; x<respdata.data.length; x++)
          {
            // if(respdata.data[x].l <= respdata.min_level)
            // {
            //     break;
            // }
            if ( respdata.data[x].cl === 'r' )
            {
              respdata.data[x].cl = 'e';
            }
            respdata.data[x].time = Math.round(traceInfoService.tscTicksToNs(respdata.data[x].ts));
            respdata.data[x].duration_tsc = respdata.data[x].it + respdata.data[x].of;
            respdata.data[x].it = Math.round(respdata.data[x].it * tscTickToMs);
            respdata.data[x].of = Math.round(respdata.data[x].of * tscTickToMs);
            respdata.data[x].duration = respdata.data[x].it + respdata.data[x].of;
            respdata.data[x].tPerIns = Math.round(respdata.data[x].duration / respdata.data[x].in);
            respdata.data[x].level = new Array(respdata.data[x].l - minLevel + 1).join('-') + '| ';
            respdata.data[x].parent = null;
            if (respdata.data[x].it>maxDuration) {
              maxDuration = respdata.data[x].it;
            }
          }
          for(var y=0; y<respdata.data.length; y++) {
            if(respdata.data[y].cl === 'c')
            {
              respdata.data[y].bgColor = getScaledCallRowColor(respdata.data[y].it, maxDuration);
            }
          }

          /**
          * Traces may not start from minimum level because of time limitation
          * -path starting point to show 0 level at the beginning
          */
          /* Disabled for now...
          var first = respdata.data[0];
          if ( first.l > respdata.min_level )
          {
            var level = '';
            for( var y = 1; y <= first.l; y++ )
            {
              level = new Array(first.l-y).join('-');
              respdata.data.unshift({'cl':'rx','cpu':'?','id':first.id,'in':0,'it':0,'l':first.l-y,'mod':'Unknow','of':0,'sym':level + '| ' +'Out Of Range','tgid':0,
              'ts': + '| ' + first.ts-y});
            }
          }
          */
          scope.calls = respdata.data;

          // Scroll to Beginning of the Graph
          var target = $('#insflow-'+scope.$id).offset().top - $rootScope.paddingTop - 10;
          $('body,html').animate({scrollTop: target}, 'slow');

        }).error(function(/*data, status, headers, config*/) {
          // called asynchronously if an error occurs
          // or server returns response with status
          // code outside of the <200, 400) range
          scope.calls = 'ERROR';
        });

        /* Helper to mange array */
        function insertArrayAt(array, index, arrayToInsert)
        {
          Array.prototype.splice.apply(array, [index, 0].concat(arrayToInsert));
          return array;
        }
      }
    };
  }
})();
