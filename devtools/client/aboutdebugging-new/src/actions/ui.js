/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PAGE_SELECTED,
} = require("../constants");

function selectPage(page) {
  return {
    type: PAGE_SELECTED,
    page,
  };
}

module.exports = {
  selectPage,
};
