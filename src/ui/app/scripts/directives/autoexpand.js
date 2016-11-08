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
    .directive('autoexpand', autoexpand);

  autoexpand.$inject = ['$window'];

  function autoexpand($window) {
    return {
      restrict: 'A',
      link: function postLink(scope, element) {

      var window = angular.element($window);

        element.bind('mouseenter', function ()
        {
          if (element.css('position') !== 'fixed')
          {
            var csTop = element.offset().top - (window).scrollTop();
            element.css({
              position: 'fixed',
              top: csTop + 'px',
            });
          }
          element.addClass('wide-call-stack');
        });

        element.bind('mouseleave', function ()
        {
          element.removeClass('wide-call-stack');
        });
      }
    };
  }
})();
