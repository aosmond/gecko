/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is loaded into the browser window scope.
/* eslint-env mozilla/browser-window */

/**
 * Global browser interface with graphics utilities.
 */
var gGfxUtils = {
  _isRecording: false,
  _isTransactionLogging: false,

  init() {
    if (Services.prefs.getBoolPref("gfx.webrender.enable-capture")) {
      this.addWrCaptureCmd();
    }
  },

  addWrCaptureCmd() {
    let command = document.createXULElement("command");
    command.setAttribute("id", "wrCaptureCmd");
    command.addEventListener("command", this.webrenderCapture, true);
    command.setAttribute("oncommand", "void 0;"); // Needed - bug 371900
    document.getElementById("mainCommandSet").prepend(command);

    let key = document.createXULElement("key");
    key.setAttribute("id", "key_wrCaptureCmd");
    if (AppConstants.platform == "macosx") {
      key.setAttribute("key", "3");
      key.setAttribute("modifiers", "control,shift");
    } else {
      key.setAttribute("key", "#");
      key.setAttribute("modifiers", "control");
    }
    key.setAttribute("command", "wrCaptureCmd");
    key.setAttribute("oncommand", "void 0;"); // Needed - bug 371900
    document.getElementById("mainKeyset").prepend(key);
  },

  /**
   * Toggle composition recording for the current window.
   */
  toggleWindowRecording() {
    window.windowUtils.setCompositionRecording(!this._isRecording);
    this._isRecording = !this._isRecording;
  },
  /**
   * Trigger a WebRender capture of the current state into a local folder.
   */
  webrenderCapture() {
    window.windowUtils.wrCapture();
  },

  /**
   * Toggle transaction logging to text file.
   */
  toggleTransactionLogging() {
    window.windowUtils.setTransactionLogging(!this._isTransactionLogging);
    this._isTransactionLogging = !this._isTransactionLogging;
  },
};
