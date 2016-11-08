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
/* jshint loopfunc: true */

(function () {

  'use strict';

  angular
    .module('satt')
    .directive('satGraph', satGraph);

  satGraph.$inject = ['$http', '$compile', '$rootScope', '$routeParams', '$timeout', 'graphService',
    'broadcastService', 'processInfo', 'traceInfo', 'bookmark', '$window', 'Flash'];

  function satGraph($http, $compile, $rootScope, $routeParams, $timeout, graphService, bcService,
      processInfoService, traceInfoService, bookmarkService, $window, Flash) {

  // constants
  var marginRight = 20;
  var marginTop = 20;
  var width = $window.innerWidth - marginRight;
  var height = 250 - marginTop /* - .5 */;

  return {
    templateUrl : 'views/sat_graph.html',
    restrict: 'E',
    replace: false,
    priority: 0,
    scope: true,
    link: function graphLink(scope, element, attrs) {

      /**
      * Scope constans
      */
      scope.threadOrProcessView = true;
      scope.fixed = false;
      scope.top = 0;
      var cpuButtonPressed = null;
      scope.overflow = { 'active':false, 'data':null };
      scope.lost = { 'active':false, 'data':null };
      scope.httpRequestActive = 0;

      /* This is guaranteed by routeProvider resolve */
      //var tscTickToMs = 1 / (parseInt(traceInfoService.getData().TSC_TICK, 10) / 1000000);

      /**
      * Destroy and cleanup scope and graphWindow
      */
      scope.$on('$destroy', function () {
        bookmarkService.removeGraph(scope.$id);
        if(scope.fixed)
        {
          graphService.removeElement(scope.$id);
        }
        $(element).remove();
      });

      var GraphSupport = function () {
        this.start = function (x) {
          //Start time in graph
          scope.start = x;
          this.scale();
        };
        this.end = function (x) {
          //Stop time in graph
          scope.end = x;
          this.scale();
        };
        this.selEnd = function (x) {
          if(x)
          {
            this._selEnd = x;
            var e = Math.round(this.rxScale(x));
            /* Check that we are not selecting over scale */
            if ( e > scope.end ) {
              e = scope.end;
              this._selEnd = x = Math.round(this.xScale(e));
            }
            if(x < this._selStart) {
              scope.selEnd = Math.round(this.rxScale(this._selStart));
              scope.selStart = e;
            } else {
              scope.selEnd = e;
            }

            scope.$apply();
          }
          return this._selEnd;
        };
        this.selStart = function (x) {
          if (x === -1)
          {
            this._selStart = null;
            delete scope.selStart;
            scope.$apply();
          }
          else if (x)
          {
            this._selStart = x;
            scope.selStart = Math.round(this.rxScale(x));
            scope.$apply();
          }
          return this._selStart;
        };
        this.zoomin = function (s, e){
          scope.start = Math.round(s);
          scope.end = Math.round(e);
        };
        this.scale = function () {
          delete this.rxScale;
          delete this.xScale;
          this.rxScale = d3.scale.linear()
            .domain([0, data.pixels])
            .range([scope.start, scope.end]);
          this.xScale = d3.scale.linear()
            .domain([scope.start, scope.end])
            .range([0, data.pixels]);
        };
        this.selMin = function () {
          return Math.min(this._selStart, this._selEnd);
        };
        this.selMax = function () {
          return Math.max(this._selStart, this._selEnd);
        };
      };

      var graphObj = new GraphSupport();
      var selectObj = {};
      var dragging = false;


      /**
       * Bookmark initiliazion after short delay
       */
      $timeout(function() {
        if ( attrs.overflow === 'true' ) {
          scope.showOverflows();
        }
        if ( attrs.lost === 'true' ) {
          scope.showLost();
        }
      }, 3000);

      /**
      * Get data from the Server
      */
      var data=null;

      //var data = graphService.getData(width - marginRight - 2, scope.startt, scope.endt);
      var graphUrl = null;

      if ( parseInt(attrs.startt, 10) === 0 && parseInt(attrs.endt, 10) === 0) {
        graphUrl = '/api/1/graph/'+$routeParams.traceID+'/full/'+(width - marginRight - 2);
      }
      else {
        graphUrl = '/api/1/graph/'+$routeParams.traceID+
              '/' + (width - marginRight - 2) +
              '/' + Math.round(attrs.startt) +
              '/' + Math.round(attrs.endt);
      }
      $http({method: 'GET', url: graphUrl})
      .success(function(respdata/*, status, headers, config*/) {
          data = respdata;
          /*
          * init GraphObj
          */
          graphObj.start(data.start);
          graphObj.end(data.end);
          gObj.initData();
          gObj.draw();

          // Check for Search Hits
          checkSearchHits(scope);

          // Scroll to Beginning of the Graph
          var target = $('#graph-'+scope.$id).offset().top;
          $('body,html').animate({scrollTop: target}, 'slow');

        })
      .error(function(/*data, status, headers, config*/) {
        // called asynchronously if an error occurs
        // or server returns response with status
        // code outside of the <200, 400) range
      });

      bookmarkService.addGraph(scope.$id,attrs.startt,attrs.endt);

      /**
      * Window resize Handling
      */
      //TODO howto resize properly?
      //http://bl.ocks.org/1276555
      $window.onresize=function() {
        width=$window.innerWidth - marginRight;
        /* Hack to clear all selection not to screw up everything */
        bcService.bcClearAllSelections();
      };

      /**
      * Scope watches
      */
      scope.$watch('start', function() {
        if (data) {
          graphObj.scale();
        }
      });
      scope.$watch('end', function() {
        if (data) {
          graphObj.scale();
        }
      });

      /**
      * Initialize Graphs
      */
      var graphInit = d3.select(element[0]).select('.graph');
      var graph = graphInit
        .append('svg:svg')
        .attr('height', height)
        .attr('viewBox', '0 0 '+ (width + 3) +' '+height)
        .attr('preserveAspectRatio','none');

      var vis = graph.append('svg:g')
        .attr('class', 'bar')
        .style('pointer-events', 'none');

      var ruler = graph.append('svg:g')
        .attr('class', 'rulers')
        .style('pointer-events', 'none');

      var overlay = graph.append('svg:g')
        .attr('class', 'overlay');

      var codeSelection = overlay
        .append('svg:rect');

      /**
      * Menu handling overflow
      */
      var drawOverflows = function() {
        bookmarkService.addGraphAttr(scope.$id,{overflow:true});
        overlay.append('g').attr('class','overflow_hits')
          .selectAll('line').data(scope.overflow.data)
          .enter().append('line')
            .attr('class','overflow_hit')
            .attr('x1', function(d) { return graphObj.xScale(d.ts); })
            .attr('x2', function(d) { return graphObj.xScale(d.ts); })
            .attr('y1', function(d) { return ( 240 / scope.cpus.length )  * d.cpu; } /* 0 */)
            .attr('y2', function(d) { return ( 240 / scope.cpus.length )  * (1+d.cpu); } /*240*/ );
      };

      scope.showOverflows = function()
      {
        if(!scope.overflow.data)
        {
          scope.httpRequestActive += 1;
          scope.overflow.active = true;
          $http({method: 'GET', url: '/api/1/search/overflow/'+$routeParams.traceID+
            '/'+(width - marginRight - 2)+'/'+scope.start+'/'+scope.end })
          .success(function(respdata /*, status, headers, config */) {
            scope.overflow.data = respdata.data;
            drawOverflows();
            scope.httpRequestActive -= 1;
          })
          .error(function(/* data, status, headers, config */) {
            scope.overflow.active = false;
            console.log('Error fetching search');
            scope.httpRequestActive -= 1;
          });
        }
        else
        {
          if (scope.overflow.active)
          {
            bookmarkService.addGraphAttr(scope.$id,{overflow:false});
            scope.overflow.active = false;
            overlay.select('.overflow_hits').remove();
          }
          else
          {
            scope.overflow.active = true;
            drawOverflows();
          }
        }
      };

      /**
      * Menu handling lost
      */
      var drawLost = function()
      {
        bookmarkService.addGraphAttr(scope.$id,{lost:true});
        overlay.append('g').attr('class','lost_hits')
          .selectAll('line').data(scope.lost.data)
          .enter().append('line')
            .attr('class','lost_hit')
            .attr('x1', function(d) { return graphObj.xScale(d.ts); })
            .attr('x2', function(d) { return graphObj.xScale(d.ts); })
            .attr('y1', function(d) { return ( 240 / scope.cpus.length )  * d.cpu; } /* 0 */)
            .attr('y2', function(d) { return ( 240 / scope.cpus.length )  * (1+d.cpu); } /*240*/ );
      };

      scope.showLost = function()
      {
        if(!scope.lost.data)
        {
          scope.httpRequestActive += 1;
          scope.lost.active = true;
          $http({method: 'GET', url: '/api/1/search/lost/'+$routeParams.traceID+
            '/'+(width - marginRight - 2)+'/'+scope.start+'/'+scope.end })
          .success(function(respdata /*, status, headers, config */) {
            scope.lost.data = respdata.data;
            drawLost();
            scope.httpRequestActive -= 1;
          })
          .error(function(/* data, status, headers, config */) {
            scope.lost.active = false;
            console.log('Error fetching search');
            scope.httpRequestActive -= 1;
          });
        }
        else
        {
          if (scope.lost.active)
          {
            scope.lost.active = false;
            overlay.select('.lost_hits').remove();
            bookmarkService.addGraphAttr(scope.$id,{lost:false});
          }
          else
          {
            scope.lost.active = true;
            drawLost();
          }
        }
      };

      /**
      *  BroadCast handlers
      */
      scope.$on('BcCodePoint', function(event,msg) {
        msg.end = msg.start + msg.duration;
        if (msg.start > 0 && !(
             ( scope.start >= msg.end ) || ( scope.end <= msg.start ))
          )
        {
          var start = graphObj.xScale(msg.start);
          var end = graphObj.xScale(msg.start + msg.duration);
          var width = Math.abs(start - end);
          var code = 'code';
          if (width < 2) {
            width = 2;
          }
          if ( width > 10 )
          {
            code = 'code_big';
          }
          codeSelection
            .attr('class', code)
            .attr('x', start)
            .attr('y', 0)
            .attr('width', width)
            .attr('height', height);
        }
        else
        {
          codeSelection
            .attr('x', 0)
            .attr('y', 0)
            .attr('width', 0)
            .attr('height', 0);
        }
      });

      /**
      * BC Code Area, showing Zoom position in other graphs
      */
      scope.$on('BcCodeArea', function(event,msg) {
        if (msg.id === scope.$id) { return; }
        if ( ! graphObj.xScale) { return; }
        if ( msg.start > scope.end || msg.end < scope.start ) { return; }
        if ( msg.start < scope.start && msg.end > scope.end ) { return; }

        if (!(msg.start === scope.start && msg.end === scope.end))
        {
          if (msg.start < scope.start) { msg.start=scope.start; }
          if (msg.end > scope.end) { msg.end=scope.end; }
          var xS = graphObj.xScale(msg.start);
          var xE = graphObj.xScale(msg.end);
          codeSelection
            .attr('class', 'code_area')
            .attr('x', Math.max(xS,0))
            .attr('y', 0)
            .attr('width', Math.abs(xE-xS))
            .attr('height', height);
        }
      });

      /**
      * Broad Cast Area Clear
      */
      scope.$on('BcCodeAreaClear', function(/*event*/) {
          codeSelection
            .attr('x', 0)
            .attr('y', 0)
            .attr('width', 0)
            .attr('height', 0);
        });

      /**
      * Broad Cast Search Hit!
      */
      var checkSearchHits = function(scope) {
        var symbolId = bcService.bcGetSearchSymbolId();
        if (symbolId > 0) {
          fetchSearchHits(symbolId,scope);
        }
      };
      var fetchSearchHits = function(symbolId,scope) {
        $http({method: 'GET', url: '/api/1/search/'+$routeParams.traceID+
          '/'+(width - marginRight - 2)+'/'+scope.start+'/'+scope.end+'/'+symbolId})
        .success(function(respdata /*, status, headers, config */) {
          overlay.append('g').attr('class','search_hits')
            .selectAll('line').data(respdata.data)
            .enter().append('line')
              .attr('class','search_hit')
              .attr('x1', function(d) { return graphObj.xScale(d.ts); })
              .attr('x2', function(d) { return graphObj.xScale(d.ts); })
              .attr('y1', function(d) { return ( 240 / scope.cpus.length )  * d.cpu; } /* 0 */)
              .attr('y2', function(d) { return ( 240 / scope.cpus.length )  * (1+d.cpu); } /*240*/ );
          $rootScope.globalConfig.fetchingSymbol = -1;
        })
        .error(function(/* data, status, headers, config */) {
          console.log('Error fetching search');
          $rootScope.globalConfig.fetchingSymbol = -1;
        });
      };

      scope.$on('BcSearchHit', function(event,symbolId) {
        fetchSearchHits(symbolId,scope);
      });

      scope.$on('BcClearSearchHit', function() {
        overlay.select('.search_hits').remove();
      });

      /**
      * Broad Cast Clear All selection
      */
      scope.$on('bcClearAllSelections', function(/*event,msg*/) {
        clearSelection();
      });

      /**
      * When window is in fixed position, it may change
      */
      scope.$on('bcWinPosition', function(event,msg) {
        if(scope.fixed)
        {
          scope.top = msg[scope.$id];
          $(element).css('top', scope.top);
        }
      });

      /**
       * Clear selection window
       */
      var clearSelection = function() {
        overlay.select('.selGroup').remove();
        graphInit.select('.selActions').remove();
        graphObj.selStart(-1);
      };

      /**
       * Graph selection Mouse handling
       */
      var handleMouseDown = function() {
        if (d3.event.which !== 1) { return; }
        if (!dragging) {
          /*
          * Clear selection on 3rd click
          */
          if ( graphObj.selStart() ) {
            clearSelection();
            return;
          }

          dragging = true;
          graphObj.selStart(d3.mouse(this)[0]);
          selectObj.selectRect = overlay
            .append('g')
            .attr('class', 'selGroup')
            .append('svg:rect')
            .attr('class', 'selection')
            .style('fill', '#999')
            .style('fill-opacity', 0.3);

          d3.event.preventDefault();
        }
        else {
          /*
          * Selection Done / Dragging ended -  ( Second click from the Mouse )
          * Area selection Completed
          * -Create grey box
          * -Add Action links
          */
          dragging = false;

          // Remove mouse handling from the graph when mouse is on top of selection
          //TODO WTF? graph.on("mousedown", null);

          /*
          * We need a scale selection to raise selAction center of the selection, when page has been resized
          */
          var someScale = d3.scale.linear()
            .domain([0, data.pixels])
            .range([0, (width - 20)]);

          var placex = someScale(graphObj.selMin()) + 10;
          if (graphObj.selMax() - graphObj.selMin() > 240) {
            placex =  someScale(graphObj.selMin()) + ((( someScale(graphObj.selMax()) - someScale(graphObj.selMin()) ) - 240 ) / 2 );
          }

          /*
          * Draw Selection box
          */
          graphInit.insert('div', 'svg')
            .attr('class', 'selActions well')
            .on('mouseover', function() {
              graphInit.select('.selActions')
              .style('display', 'block');
            })
            .on('mouseout', function() {
              graphInit.select('.selActions')
              .style('display', 'none');
            })
            .style('left', placex +'px')
            .html('<span>Selection:</span>'+
              '<ul class="nav nav-pills nav-stacked">'+
              '<li><a class="newGraph" href="#" onclick="return false;">Open New Graph</a></li>'+
/* TODO: Disabled NOW, until fixed properly '<li><a class="zoomIn" href="#" onclick="return false;">Zoom Into Selection</a></li>'+*/
              '<li><a class="cancelSel" href="#" onclick="return false;">Cancel Selection</a></li>'+
              '</ul>');

          /*
          * Selection Box Action
          *  - Open New graph
          */
          graphInit.select('.newGraph')
          .on('mousedown', function() {
            /** Adding dynamically new items Directives to scole / DOM */
            var newG = d3.select('.app-page')
              .append('sat-graph')
              .attr('window-manager',0)
              .attr('startt',graphObj.rxScale(graphObj.selMin()))
              .attr('endt',graphObj.rxScale(graphObj.selMax()));

            // Clear selection when stating new graph
            clearSelection();

            //Attach new item to scope
            $compile(newG[0])(scope.$parent);
            scope.$parent.$apply();

          });

          /*
          * Selection Box Action
          *  - Zoom IN
          */
          graphInit.select('.zoomIn')
          .on('mousedown', function() {
            graphObj.zoomin(graphObj.rxScale(graphObj.selMin()),
                    graphObj.rxScale(graphObj.selMax()));
            //delete selection
            overlay.select('.search_hits').remove();
            clearSelection();
            //update scope
            scope.$apply();
            //draw();
            $http({method: 'GET', url: '/api/1/graph/'+$routeParams.traceID+'/'+(width - marginRight - 2)+'/'+scope.start+'/'+scope.end})
            .success(function(respdata/*, status, headers, config*/) {
              data = respdata;
              /*
              * init GraphObj
              */
              gObj.initData();
              gObj.draw();
              /* Check if Search hits are wanted */
              checkSearchHits(scope);
            });
          });
          /*
          * Selection Box Action
          *  - cancel selection
          */
          graphInit.select('.cancelSel')
          .on('mousedown', function() {
            //delete selection
            clearSelection();
          });

          overlay.select('.selection')
          .on('mouseover', function() {
            graphInit.select('.selActions').style('display', 'block');
            graph.on('mousedown', null);
          })
          .on('mouseout', function() {
            graphInit.select('.selActions').style('display', 'none');
            graph.on('mousedown', handleMouseDown);
          });
        }
      }; //MouseDown
      graph.on('mousedown', handleMouseDown);


      var handleMouseMove = function() {
        if (dragging) {
          graphObj.selEnd(d3.mouse(this)[0]);
          d3.select(this)
            .select('.selection')
            .attr('x', graphObj.selMin())
            .attr('y', 0)
            .attr('width', graphObj.selMax()-graphObj.selMin())
            .attr('height', height);
        }
      };
      graph.on('mousemove',handleMouseMove);


      var handleMouseOver = function() {
        if (graphObj.selStart()) {
          d3.select(this).select('.selection').style('display', 'block');
        }
        var area = {};
        area.start = Math.min(scope.start, scope.end);
        area.end = Math.max(scope.start,scope.end);
        area.id = scope.$id;
        bcService.bcCodeArea(area);
      };
      graph.on('mouseover',handleMouseOver);

      var handleMouseOut = function() {
        bcService.bcCodeAreaClear();
        if (!graphObj.selStart() && dragging) { return; }
        if ( d3 && d3.event && d3.event.toElement &&
              d3.event.toElement.className &&
              d3.event.toElement.className === 'selActions well') { return; }
        d3.select(this).select('.selection').style('display', 'none');
      };
      graph.on('mouseout',handleMouseOut);

      /**
      * cpu_mouseclick
      */
      scope.cpuMouseclick = function(cpu) {
        var cpuNumber = parseInt(cpu.charAt(cpu.length-1),10);
        if ( cpuButtonPressed !== cpuNumber )
        {
          cpuButtonPressed = cpuNumber;
          gObj.showCpu(cpu);
          $(element).find('button.'+cpu).addClass('selected');
          $(element).find('button:not(.'+cpu+').cpu-button').removeClass('selected');
        }
        else
        {
          cpuButtonPressed = null;
          gObj.showAllCpus();
          $(element).find('button.'+cpu).removeClass('selected');
        }
      };

      /**
      * Core bus ratio
      */
      scope.cbrClick = function() {
        if ( ! scope.cbrVisible ) {
          var cbrUrl = '/api/1/cbr/'+$routeParams.traceID +
                '/' + Math.round(attrs.startt) +
                '/' + Math.round(attrs.endt);

          // TODO this all could go to service
          $http({method: 'GET', url: cbrUrl})
          .success(function(respdata/*, status, headers, config*/) {
            // Initialize tmp variables
            var cbrData = [];
            var acbr = [];
            var ecbr = [];
            for(var x=0; x<scope.cpus.length; x++)
            {
              cbrData[x] = [];
              acbr[x] = 0xff;
              ecbr[x] = 0xff;
            }

            var tsStart = [];
            var cpu;
            for (var i = 0; i < respdata.data.length; i++) {
              cpu = respdata.data[i].cpu;
              if (!tsStart[cpu])
              {
                // Initialize start values
                tsStart[cpu] = respdata.data[i].ts;
              }
              if ( acbr[cpu] !== respdata.data[i].acbr || ecbr[cpu] !== respdata.data[i].ecbr)
              {
                if (tsStart[cpu] !== respdata.data[i].ts)
                {
                  var data = respdata.data[i];
                  var tsTmp = respdata.data[i].ts;
                  data.tsEnd = respdata.data[i].ts - 1;
                  data.ts = tsStart[cpu];
                  tsStart[cpu] = tsTmp;
                  cbrData[cpu].push(data);
                }
              }
            }

            // Scale for the Y graph
            /*
              TODO needs prober CBR range detection for devices
              now 28 is ok for cht based devices
            */
            var yScaleCbr = d3.scale.linear()
              .domain([28, 0])
              .rangeRound([marginTop, height ]);

            for(var cpus=0; cpus<cbrData.length;cpus++)
            {
              vis.select('.cpu'+cpus+'.transform').append('g').attr('class','cbr')
                .selectAll('line').data(cbrData[cpus])
                .enter().append('line')
                  .attr('class','ecbr')
                  .attr('x1', function(d) { return graphObj.xScale(d.ts); })
                  .attr('x2', function(d) { return graphObj.xScale(d.tsEnd); })
                  .attr('y1', function(d) { return yScaleCbr(d.ecbr); })
                  .attr('y2', function(d) { return yScaleCbr(d.ecbr); });

              vis.select('.cpu'+cpus+'.transform').append('g').attr('class','cbr')
                .selectAll('line').data(cbrData[cpus])
                .enter().append('line')
                  .attr('class','acbr')
                  .attr('x1', function(d) { return graphObj.xScale(d.ts); })
                  .attr('x2', function(d) { return graphObj.xScale(d.tsEnd); })
                  .attr('y1', function(d) { return yScaleCbr(d.acbr); })
                  .attr('y2', function(d) { return yScaleCbr(d.acbr); } );
            }
          })
          .error(function(/*data, status, headers, config*/) {
            // called asynchronously if an error occurs
            // or server returns response with status
            // code outside of the <200, 400) range
          });
        }
        else
        {
          vis.selectAll('.cbr').remove();
        }
        scope.cbrVisible = !scope.cbrVisible;
      };

      scope.openStatistics = function() {
        var newS = d3.select('.app-page')
          .append('statistics')
          .attr('window-manager',0)
          .attr('start',scope.start)
          .attr('end',scope.end);
        $compile(newS[0])(scope.$parent);
      };

      /**
      * Process button handling
      */
      var mouseEnterPromise = null;

      scope.pMouseEnter = function(index, mousePos) {
        var i = index;
        var me = mousePos;
        mouseEnterPromise = $timeout(function() {
          handleMouseEnterTimout(i, me);
        }, 400);
      };

      var handleMouseEnterTimout = function(index, mousePos) {
        /* Calculate threadInfo / processInfo box position */
        var posX = mousePos.clientX;
        var posY = mousePos.clientY;
        scope.threadInfoStyle = {};
        if ($window.innerWidth-250 < posX) {
          scope.threadInfoStyle.right = 0 + 'px';
        } else {
          scope.threadInfoStyle.left = posX + 'px';
        }
        if ($window.innerHeight-250 < posY) {
          scope.threadInfoStyle.bottom = $window.innerHeight - posY + 10 + 'px';
        } else {
          scope.threadInfoStyle.top = posY + 10 + 'px';
        }

        if (scope.threadOrProcessView) {
          showThread(index);
        } else {
          showProcess(index);
        }
      };

      var showProcess = function(index) {
        var process = scope.mainProcesses[index];
        var childrens = _.filter(scope.processes, {'tgid':process.tgid});
        scope.childThreads = _.map(childrens, function(child) {
          return {n:child.n, tid:child.p, high:''};
        });
        var tgid = process.tgid;
        var ti = { pn: process.pn, tgid: process.tgid, n: process.n, tid: process.tid };

        scope.threadInfo = ti;
        graph.selectAll('.bar').selectAll('rect:not(.t'+tgid+')')
          .attr('y', function(/*d*/) { return (height); })
          .attr('height', 0);
        graph.selectAll('.bar').selectAll('rect.t'+tgid)
          .attr('y', function(d) { return height - yScale(d.h); })
          .attr('height', function(d) { return yScale(d.h); });
      };

      var showThread = function(index) {
        var process = scope.processes[index];
        var childrens = _.filter(scope.processes, {'tgid':process.tgid});
        scope.childThreads = _.map(childrens, function(child) {
          return {n:child.n, tid:child.p, high:process.p===child.p?'high':''};
        });
        var pid = process.p;
        var ti = { pn: process.pn, tgid: process.tgid, n: process.n, tid: process.tid };

        scope.threadInfo = ti;
        graph.selectAll('.bar').selectAll('rect:not(.p'+pid+')')
           /* .transition()
          .duration(300) */
          .attr('y', function(/*d*/) { return (height); })
          .attr('height', 0);
        graph.selectAll('.bar').selectAll('rect.p'+pid)
          /* .transition()
          .duration(300) */
          .attr('y', function(d) { return height - yScale(d.h); })
          .attr('height', function(d) { return yScale(d.h); });
      };

      scope.pMouseLeave = function(/*pid*/) {
        $timeout.cancel(mouseEnterPromise);
        if (scope.threadInfo)
        {
          scope.threadInfo = false;
          graph.selectAll('.bar').selectAll('rect')
          /*.transition()
          .duration(300)*/
          .attr('y', function(d) { return height - yScale(d.h) - yScale(d.y); })
          .attr('height', function(d) { return yScale(d.h); });
        }
      };

      scope.pMouseClick = function(index) {
        // Adding dynamically new items Directives to scole / DOM
        if (!scope.threadOrProcessView) {
          Flash.showDanger('Please open Function flow from Thread view!');
          return;
        }

        var newG = d3.select('.app-page')
          .append('insflow')
          .attr('window-manager',0)
          .attr('id',scope.processes[index].id)
          .attr('tgid',scope.processes[index].tgid)
          .attr('pid',scope.processes[index].p)
          .attr('pname',scope.processes[index].n)
          .attr('start',scope.start)
          .attr('end',scope.end);
        //Attach new item to scope
        $compile(newG[0])(scope.$parent);

        //scope.$parent.$apply();
      };

      /* Switch thread / process view */
      scope.setThreadView = function(threadView) {
        if (scope.threadOrProcessView === threadView) {
          return;
        }
        var processes = scope.processes;
        scope.threadOrProcessView = threadView;
        // Process View
        if (!scope.threadOrProcessView) {
          var uniqProcesses = _.uniq(processes, function(p) { return p.tgid; });
          _.forEach(uniqProcesses, function(p) {
            $(element).find('rect.t'+p.tgid.toString()).css('fill', graphService.getColor(p.tgid));
          });
        } else {
          _.forEach(processes, function(p) {
            $(element).find('rect.p'+p.p.toString()).css('fill', graphService.getColor(p.p));
          });
        }
      };


    /********************************************************
    *
    *  Grah drawing
    *
    *  Considire - Own class which takes input and output
    *
    *********************************************************/

      var yScale = null;
      /**
      * Drawing the Graph
      */
      var GraphC = function() {

        //Holding graph class name e.g. cpu1, cpu2 etc
        var processInfo = [];
        var processInfoTmp = [];
        var xScale;
        var yScaleIn;
        var cpuScope = [];
        var graphHeight = height;
        var scale = 1;

        /* Helper function to get max */
        var getMaxYSum = function() {
          var yMax = 0;
          _.forIn(data.ySum,function(yData/*,key*/) {
            yMax = _.max([ _.max(yData), yMax ]);
          });
          return yMax;
        };

        /**
        * Do the final calculation for the received data
        */
        this.initData = function(/*processView*/) {
          //Clear CPU count
          cpuScope = [];
          /* Graph drawing needs there */
          graphHeight = 230 - marginTop - (data.cpus * 2);
          scale = (( graphHeight ) / 230) / data.cpus;

          /* Create CPU scope object */
          var x=0;
          for (x=0;x<data.cpus;x++)
          {
            cpuScope.push({'n':'cpu '+x, 'class':'cpu'+x, 'id':x});
          }
          scope.cpus = cpuScope;

          /* Final format is calculated here */
          data.ySum = [];
          data.yGroup = [];

          for (var cpu in data.traces)
          {
            var sum = [];
            data.yGroup[cpu] = [];

            for(x=0; x<data.pixels; x++)
            {
              var y=[];
              var subSum = 0;
              //Calculate Total SUM

              data.traces[cpu].forEach(function(d) {
                if(d.data[x])
                {
                  /* Special case to handle swapper d.p == 0 */
                  if (d.p !== 0)
                  {
                    y.push({'h':d.data[x]+1,'y':subSum, 'p':d.p, 't':d.tgid, 'c':graphService.getColor(d.p)});
                  }
                  else
                  {   // swapper/0
                    var newPid = parseInt(d.n.substring(8,9),10);
                    y.push({'h':d.data[x]+1,'y':subSum, 'p':newPid, 't':d.tgid, 'c':graphService.getColor(newPid)});
                  }

                  subSum += d.data[x];
                }
                else
                {
                  // Less data in screen
                  //y.push({'h':0,'y':0, 'p':d.p });
                }
              });
              sum.push(subSum);
              data.yGroup[cpu].push(y);
            }
            data.ySum[cpu] = sum;
          }

          /*
          * Loop trough data to Get all Processes info
          */
          for (var tmpCpu in data.traces)
          {
            data.traces[tmpCpu].forEach(function(d) {
              var processId = d.p;
              /* Special case to handle swapper d.p == 0 */
              if ( d.p === 0)
              {
                processId = parseInt(d.n.substring(8,9),10);
              }

              if ( $.inArray(processId, processInfoTmp) === -1 )
              {
                processInfoTmp.push(processId);
                processInfo.push({'id':d.id, 'n':d.n, 'p':processId, 'tid':d.p, 'tgid':d.tgid, 'color': { 'background-color': graphService.getColor(processId) }, 'sum':_.reduce(d.data, function(sum,num){return sum + num;})});
              }
              else
              {
                var pi = _.find(processInfo,{'p':processId});
                pi.sum = pi.sum + _.reduce(d.data, function(sum,num){return sum + num;});
              }

              /* Check if this is process Head */
              if (processId === d.tgid)
              {
                /* Push process name to service to store them */
                processInfoService.addItem({'tgid':d.tgid, 'pn':d.n});
              }
            });
          }

          /* Another loop to add process names */
          _.forEach(processInfo, function(item){
            item.pn = processInfoService.getProcessNameByTgid(item.tgid);
          });

          /**
          * Push Threads and Processes Info in to the scope
          * - and sort and reverse sort by called items
          */
          scope.processes = _.sortBy(processInfo, 'sum').reverse();
          var mainProcesses = [];
          _.forEach(processInfo, function(p) {
            var process = _.find(mainProcesses, {'tgid':p.tgid});
            if (process) {
              if (p.tgid===p.p) {
                process = p;
              }
            } else {
              mainProcesses.push(p);
            }
          });
          _.forEach(mainProcesses, function(p) {
            p.color = { 'background-color': graphService.getColor(p.tgid) };
            var sum = _.reduce(_.pluck(_.filter(processInfo, {'tgid':p.tgid}), 'sum'), function(sum, num) {
              return sum + num;
            });
            p.sum = sum;
          });
          scope.mainProcesses = _.sortBy(mainProcesses, 'sum').reverse();

          /**
          * Data finalization done
          */
          var maxY = getMaxYSum();
          yScale = d3.scale.linear()
            .domain([0, maxY])
            .rangeRound([0, height]);
              //.rangeRound([0, height - marginTop]);
          // Scale for the Y graph
          yScaleIn = d3.scale.linear()
              .domain([maxY, 0])
              .rangeRound([marginTop, height ]);

          xScale = d3.scale.linear()
            .domain([traceInfoService.tscTicksToNs(scope.start), traceInfoService.tscTicksToNs(scope.end)])
            .range([0, data.pixels]);
        };

        /* graph drawin helper function */
        var scaleNumOfCpu = function(numberOfCpu){ return (( graphHeight ) / 230) / numberOfCpu; };

        /**
        * public draw function to draw graphs
        */
        this.draw = function() {
          /* Clear bars from the screen */
          vis.selectAll('*').remove();
          ruler.selectAll('*').remove();

          for (var cpu in data.traces)
          {
            _draw(cpu);
          }

          drawRulers();
        };

        /**
        * Show / Hide graph basend on cpu0, cpu1 class name
        */
        this.showCpu = function(cpu) {
          $(element).find('button.'+cpu).addClass('btn-info');
          $(element).find('button:not(.'+cpu+')').removeClass('btn-info');

          var scale = scaleNumOfCpu(1);
          vis.select('g.'+cpu + '.scale')
            .transition().duration(500)
            .attr('transform', 'scale( 1, '+scale+')');
          vis.selectAll('g.scale:not(.'+cpu +')')
            .transition().duration(500)
            .attr('transform', 'scale( 0, 0)');
          vis.select('g.'+cpu + '.transform')
            .transition().duration(500)
            .attr('transform', 'translate( 0, '+(marginTop / scale )+')');
        };

        this.showAllCpus = function() {
          for (var cpuNumber=0; cpuNumber < data.cpus; cpuNumber++)
          {
            var cpu = 'cpu'+cpuNumber;
            var graphYPos = (( marginTop + ( graphHeight ) / data.cpus * cpuNumber ) / scale ) + cpuNumber * 2;

            vis.select('g.'+cpu + '.scale')
              .transition().duration(500)
              .attr('transform', 'scale( 1, '+scale+')');
            vis.select('g.'+cpu + '.transform')
              .transition().duration(500)
              .attr('transform', 'translate( 0, '+graphYPos+')');
          }
        };

        /**
        * Drawing lines / graph is done HERE!
        */
        var _draw = function(cpu) {
          var cpuNumber = parseInt(cpu.charAt(cpu.length-1),10);
          var graphYPos = (( marginTop + ( graphHeight ) / data.cpus * cpuNumber ) / scale ) + cpuNumber * 2;

          var cpuG = vis.append('svg:g')
            .attr('class',cpu + ' scale')
            .attr('transform', 'scale( 1, '+scale+')' );

          cpuG = cpuG.append('svg:g')
            .attr('class',cpu + ' transform')
            .attr('transform', 'translate( 0,' + graphYPos + ')' );

          var slines = cpuG.selectAll('.bar')
            .data(data.yGroup[cpu])
          .enter().append('g')
            .attr('class', 'g')
            .attr('transform', function(d, i) { return 'translate(' + i + ',0)'; });

          slines.selectAll('rect')
            .data(function(d) { return d; })
          // RECT version
          .enter().append('rect')
            .attr('class', function(d) { return 'p'+d.p + ' t'+d.t; })
            .attr('width', 1)
            .attr('y', height)
            .attr('height', 0)
            .style('fill', function(d) { return d.c; });

          /**
          * Draw y-rulers
          */
          var yruler = cpuG.append('svg:g')
            .attr('class', 'ruler');

          var yrule = yruler.selectAll('g.y')
            .data(yScaleIn.ticks(5))
            .enter().append('svg:g')
            .attr('class', 'y');

          yrule.append('svg:line')
            .attr('class', 'yLine')
            .style('stroke', '#aaa')
            .style('shape-rendering', 'auto')
            .style('stroke-dasharray','4 4')
            .attr('x1', 0)
            .attr('x2', width )
            .attr('y1', yScaleIn )
            .attr('y2', yScaleIn );

          yrule.append('svg:text')
            .attr('class', 'yText')
            .attr('x', width )
            .attr('y', function(d) { return yScaleIn(d) - 12; } )
            .attr('dy', '.60em')
            .attr('text-anchor', 'end')
            .text(yScaleIn.tickFormat(10));

          function backToFullGraph() {
            slines.selectAll('rect')
            .transition()
            .duration(500)
            .attr('y', function(d) {
              return height - yScale(d.h) - yScale(d.y);
            })
            .attr('height', function(d) { return yScale(d.h); });
          }
          backToFullGraph();

          /* Line version
              .enter().append('line')
                .attr("x1", 0)
                .attr("y1", function(d) { return height - yScale(d.h) - yScale(d.y); })
                .attr("y2", function(d) { return height - yScale(d.y); })
                //.attr("height", function(d) { return d.h; })
                .style("stroke", function(d,i) { return color(data.traces[i].n); });
          */
        };

        /**
         * Scale time to nices real numbers
         */
        var scaleTime = function(timeInNs) {
          var s = traceInfoService.tscTicksToNs(scope.start);
          var e = traceInfoService.tscTicksToNs(scope.end);
          var length = e-s;
          if ( timeInNs > 100000000 && length > 10000000)
          {
            return timeInNs / 1000000 + 'ms';
          }
          if ( timeInNs > 100000 &&  length > 20000)
          {
            return timeInNs / 1000 + 'us';
          }
          return timeInNs + 'ns';
        };

        /**
         * Draw Rules
         */
        function drawRulers() {
          var xrule = ruler.selectAll('g.x')
            .data(xScale.ticks(10))
            .enter().append('svg:g')
            .attr('class', 'x');

          xrule.append('svg:line')
            .style('stroke', '#aaa')
            .style('shape-rendering', 'crispEdges')
            .style('stroke-dasharray','4 4')
            .attr('x1', xScale)
            .attr('x2', xScale)
            .attr('y1', 15)
            .attr('y2', height);

          xrule.append('svg:text')
            .attr('x', xScale)
            .attr('y', 3)
            .attr('dy', '.71em')
            .attr('text-anchor', 'middle')
            .style('font-color', '#aaa')
            .text(function(n) { return scaleTime(n); });
        }
      };

      //Draw first time
      //draw();
      var gObj = new GraphC(scope);
    }
  };
  }
})();
