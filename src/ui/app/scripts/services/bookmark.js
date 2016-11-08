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

  /**
   * Service to Handle Bookmarks
   * - Store UI state locally
   * - Communicate to Server
   *
   */

  angular
    .module('satt')
    .factory('bookmark', bookmarkService);

  bookmarkService.$inject = ['$http'];

  function bookmarkService($http) {

    var urlBase = '/api/1/bookmark';
    var itemOrder = 0;
    var bookmark = [];
    var bookmarkList = [];

    var obj = {
      bookmarkList: bookmarkList,
      _getBookmarkList: function () {
        /* Get Initial bookmark list */
        $http.get(urlBase).then(function (result) {
          obj.bookmarkList = result.data;
        });
      },
      saveBookmark: function (bmInfo) {
        bmInfo.data = JSON.stringify(bookmark);
        $http.post(urlBase, bmInfo)
        .success(function (respdata /*, status, headers, config */) {
          bmInfo.id = respdata.id;
          bmInfo.traceid = bmInfo.traceId;
          obj.bookmarkList.push(bmInfo);
        })
        .error(function (/* data, status, headers, config */) {
          console.log('Error fetching search');
        });
      },
      /**
       * Update only title and description
       */
      updateBookmark: function (bmInfo) {
        $http.put(urlBase, bmInfo).success(function (/*resp*/) {
          var id = _.findIndex(obj.bookmarkList, function (bml) { return bml.id === bmInfo.id; });
          if (id !== null) {
            obj.bookmarkList[id].title = bmInfo.title;
            obj.bookmarkList[id].description = bmInfo.description;
          }
        });
      },
      deleteBookmark: function (bookmarkId) {
        $http.delete(urlBase + '/' + bookmarkId).success(function () {
          var removeId = _.findIndex(obj.bookmarkList, function (bml) { return bml.id === bookmarkId; });
          if (removeId !== null) {
            obj.bookmarkList.splice(removeId,1);
          }
        });
      },

    /**
     * Bookmark item, interface functions
     */
      /**
       * Get bookmark promise
       */
      get: function (bookmarkId) {
        return $http.get(urlBase + '/' + bookmarkId);
      },
      /**
       * Clear bookmark state when view changes
       */
      clear: function () {
        bookmark.length = 0;
      },
      addStatistics: function (id, start, end) {
        bookmark.push({
          id    : id,
          order : itemOrder++,
          type  :'statistics',
          data  : { start:start, end:end }
        });
      },
      removeStatistics: function (id) {
        var removeId = _.findIndex(bookmark, function (bm) { return bm.id === id; });
        if (removeId !== null) {
          bookmark.splice(removeId,1);
        }
      },
      addGraph: function (id, start, end) {
        bookmark.push({
          id    : id,
          order : itemOrder++,
          type  :'graph',
          data  : { start:start, end:end }
        });
      },
      removeGraph: function (id) {
        var removeId = _.findIndex(bookmark, function (bm) { return bm.id === id; });
        if (removeId) {
          bookmark.splice(removeId,1);
        }
      },
      addGraphAttr: function (id, data) {
        var bmId = _.findIndex(bookmark, function (bm) { return bm.id === id; });
        if (bmId !== null) {
          bookmark[bmId].data = _.assign( bookmark[bmId].data , data );
        }
      },
      addInsflow: function (scopeId, id, tgid, pid, pname, start, end) {
        bookmark.push({
          id    : scopeId,
          order : itemOrder++,
          type  :'insflow',
          data  : { id:id, tgid:tgid, pid:pid, pname:pname, start:start, end:end }
        });
      },
      removeInsflow: function (id) {
        var removeId = _.findIndex(bookmark, function (bm) { return bm.id === id; });
        if (removeId) {
          bookmark.splice(removeId,1);
        }
      }
    };

    obj._getBookmarkList();

    return obj;
  }
})();
