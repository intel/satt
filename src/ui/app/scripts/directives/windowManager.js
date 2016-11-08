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
    .directive('windowManager', windowManager);

  windowManager.$inject = ['graphService'];

  function windowManager(graphService) {
    return {
      restrict: 'A',
      priority: 10,
      link: function postLink(scope, element) {
        /**
        * Window button handling
        */
        scope.graphClose = function () {
          //This will destroy scope, but also will call to scope listener with $destroy event to do cleanup
          scope.$destroy();
          $(element).remove();
        };

        scope.graphFixed = function () {
          scope.fixed = true;
          var height = graphService.addElement(scope.$id, $(element).innerHeight());
          $(element).css('top', height);
          $(element).addClass('graph-fixed');
        };

        scope.graphFloat = function () {
          scope.fixed = false;
          $(element).removeClass('graph-fixed');
          graphService.removeElement(scope.$id);
        };

        scope.graphToggleFold = function () {
          $(element).find('.winm').toggle();
          if (scope.fixed)
          {
            //var height = graphService.changeElementHeight(scope.$id, $(element).innerHeight());
            graphService.changeElementHeight(scope.$id, $(element).innerHeight());
          }
        };
      }
    };
  }
})();
