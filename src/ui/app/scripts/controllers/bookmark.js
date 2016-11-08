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
    .controller('BookmarkCtrl', BookmarkCtrl);

  BookmarkCtrl.$inject = ['$scope', '$routeParams', 'bookmark'];

  function BookmarkCtrl($scope, $routeParams, bookmarkService) {

    $scope.data = {
      visible: false,
      addButtonDisabled: false,
      bookmarkListButtonDisabled: false,
      bookmarkViewTypeList: 'list',
      title: 'Bookmark list',
      bookmarks: bookmarkService.bookmarksList,
    };

    /**
     * Watch bookmark list
     */
    $scope.$watch(function () { return bookmarkService.bookmarkList; }, function (bookmarks) {
      $scope.data.bookmarks = bookmarks;
    });

    /**
     * Listen Route changes
     */
    $scope.$on('$routeChangeSuccess', function (/*event*/) {

      bookmarkService.clear();

      if ($routeParams.traceID) {
        $scope.data.addButtonDisabled = false;
      }
      else {
        $scope.data.addButtonDisabled = true;
      }
    });

    $scope.showBookmarksButton = function () {
      //Toggle visiblity
      $scope.data.bookmarkViewTypeList = 'list';
      $scope.data.title = 'Bookmark list';
      $scope.data.visible = !$scope.data.visible;
    };

    /* Boomark current view */
    $scope.addBookmark = function () {
      $scope.data.bookmarkViewTypeList = 'new';
      $scope.data.visible = true;
      $scope.data.title = 'Save current view as Bookmark ';
      $scope.bookmark = {
        id: '',
        traceId: $routeParams.traceID,
        title: '',
        description: '',
      };
    };

    $scope.saveBookmark = function () {
      bookmarkService.saveBookmark($scope.bookmark);
      $scope.data.bookmarkViewTypeList = 'list';
    };

    $scope.openBookmark = function (id) {
      //console.log(id);
    };

    $scope.editBookmark = function (bookmarkId) {
      bookmarkService.get(bookmarkId).then(function (resp) {
        //var bm = JSON.parse(resp.data.data);
        var bm = resp.data;
        $scope.data.bookmarkViewTypeList = 'update';
        $scope.data.visible = true;
        $scope.data.title = 'Edit bookmark details';
        $scope.bookmark = {
          id: bm.id,
          traceId: bm.traceid,
          title: bm.title,
          description: bm.description,
        };
      });
    };

    $scope.editBookmarkUpdate = function (bookmarkId) {
      bookmarkService.updateBookmark($scope.bookmark);
      $scope.data.bookmarkViewTypeList = 'list';
    };

    $scope.deleteBookmark = function (id) {
      var r = confirm('Are you sure, you want to delete bookmark permanently?');
      if (r === true) {
        bookmarkService.deleteBookmark(id);
      }
    };
  }
})();
