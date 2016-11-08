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
    .service('Flash', Flash);

  Flash.$inject = ['$rootScope', '$timeout'];

  function Flash($rootScope, $timeout) {
    // AngularJS will instantiate a singleton by calling "new" on this function
    var messages = [];
    var nextTimeout;
    var stopTimeout;

    var _start = function() {
      var msgObj = messages.pop();
      if(msgObj) {
        $rootScope.flash = msgObj;
        nextTimeout = msgObj.timeout;
      }
      else {
        $rootScope.flash = '';
        $timeout.cancel(stopTimeout);
        stopTimeout = '';
      }
      if(msgObj) {
        stopTimeout = $timeout(_start,nextTimeout*1000);
      }
    };

    var _putMessage = function(msgObj) {
      msgObj.timeout = typeof msgObj.timeout !== 'undefined' ? msgObj.timeout : 5;
      messages.push(msgObj);
      if( ! stopTimeout )
      {
        _start();
      }
    };

    var services = {
      showSuccess: function(message,timeout) {
        _putMessage({'message':message,'type':'success','timeout':timeout});
      },
      showInfo: function(message,timeout) {
        _putMessage({'message':message,'type':'info','timeout':timeout});
      },
      showWarning: function(message,timeout) {
        _putMessage({'message':message,'type':'warning','timeout':timeout});
      },
      showDanger: function(message,timeout) {
        _putMessage({'message':message,'type':'danger','timeout':timeout});
      },
      clear: function() {
        $timeout.cancel(stopTimeout);
      }
    };

    return services;
  }
})();
