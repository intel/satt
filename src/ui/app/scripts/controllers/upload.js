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
    .controller('UploadCtrl', UploadCtrl);

  UploadCtrl.$inject = ['$scope', '$timeout', 'Flash'];

  function UploadCtrl($scope, $timeout, flash) {
    $scope.$on('flow::fileAdded', function (event, $flow, flowFile) {
      if (flowFile.getType() !== 'x-compressed-tar')
      {
        //prevent file from uploading
        event.preventDefault();
        flash.showDanger('Only x-compressed-tar *.tgz files are allowed to upload');
      }
      else
      {
        $timeout(function() {
          $flow.upload();
        }, 300);
      }
    });

    $scope.$on('flow::uploadStart', function () {
      $scope.loading = true;
      $scope.percent = 0;
    });

    $scope.$on('flow::fileProgress', function (event, $flow) {
      var precent = $flow.sizeUploaded() / $flow.getSize() * 100;
      $scope.percent = precent;
    });

    $scope.$on('flow::complete', function (event, $flow) {
      $scope.percent = 100;
      $scope.loading = false;
      flash.showSuccess('File "' + $flow.files[0].name + '" uploaded succesfully and is now in the processing. You can check the progress from the trace list view.');
        // Cleanup we don't need file in browser
        $flow.cancel();
    });

    $scope.$on('flow::error', function (/*event, $flow, flowFile*/) {
      $scope.loading = false;
      flash.showDanger('Error uploading file ');
    });
  }
})();
