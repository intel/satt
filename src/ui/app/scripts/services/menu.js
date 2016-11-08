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
    .factory('menuService', menuService);

  function menuService() {
    // Public API here
    var service = {
      //menu : [ { 'id': 0, 'active':0, 'name':'TraceList','href':'#' } ],
      menu : [],
      setActive: function($id)
      {
        for(var i=0; i<this.menu.length; i++)
        {
          if (this.menu[i].active) { this.menu[i].active = 0; }
          if (this.menu[i].id === $id) { this.menu[i].active = 1; }
        }
        return;
      },
      addItem: function($item) {
        var alreadyOpen = false;
        for (var i=0; i<this.menu.length; i++)
        {
          if (this.menu[i].id === $item.id )
          {
            alreadyOpen = true;
            break;
          }
        }
        if ( ! alreadyOpen ) {
          this.menu.length = 0;
          this.menu.push($item);
        }
        this.setActive($item.id);
        return;
      },
      removeItem: function($id) {
        return $id;
      },
    };
    return service;
  }
})();
