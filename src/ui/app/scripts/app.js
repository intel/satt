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
'use strict';

/**
 * Controlling application routes
 */
var SATT = angular.module('satt', ['ngRoute', 'ngResource', 'ui.bootstrap', 'flow', 'templates']);
SATT.config(['$locationProvider', '$routeProvider', function ($locationProvider, $routeProvider) {
  // Set HTML5 mode on
  $locationProvider.html5Mode(true);

  $routeProvider
    .when('/', {
      templateUrl: 'views/main.html',
      controller: 'MainCtrl'
    })
    .when('/trace/:traceID/:bookmarkId?', {
      templateUrl: 'views/trace.html',
      controller: 'TraceCtrl',
      resolve: {
        // Before changing into trace view, fetch traceinfo from server
        data: ['$route', 'traceInfo', function ($route, traceInfo) {
          return traceInfo.get($route.current.params.traceID);
        }]
      }
    })
    .when('/admin', {
      templateUrl: 'views/admin/main.html',
      controller: 'AdminCtrl'
    })
    .when('/admin/:traceID', {
      templateUrl: 'views/admin/edit.html',
      controller: 'AdminEditCtrl'
    })
    .otherwise({
      redirectTo: '/'
    });
}]);

SATT.config(['$httpProvider', '$provide', function ($httpProvider, $provide) {
  // register the interceptor as a service
  $provide.factory('myHttpInterceptor',  [ '$q', 'Flash', function ($q, flash) {
    return {
      // optional method
      'responseError': function (rejection) {

        // do something on error
        if (rejection.status === 404 && rejection.data.text) {
          flash.showDanger(rejection.data.text);
          return $q.reject(rejection);
        }

        return $q.reject(rejection);
      }
    };
  }]);

  $httpProvider.interceptors.push('myHttpInterceptor');

}]);

SATT.config(['flowFactoryProvider', function (flowFactoryProvider) {
  flowFactoryProvider.defaults = {
    target: '/api/1/upload',
    permanentErrors: [404, 500, 501],
    singleFile: true
  };
}]);
