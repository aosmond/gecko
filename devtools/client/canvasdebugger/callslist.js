/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* import-globals-from canvasdebugger.js */
/* globals window, document */
"use strict";

/**
 * Functions handling details about a single recorded animation frame snapshot
 * (the calls list, rendering preview, thumbnails filmstrip etc.).
 */
var CallsListView = extend(WidgetMethods, {
  /**
   * Initialization function, called when the tool is started.
   */
  initialize: function() {
    this.widget = new SideMenuWidget($("#calls-list"));
    this._searchbox = $("#calls-searchbox");
    this._filmstrip = $("#snapshot-filmstrip");

    this._onSelect = this._onSelect.bind(this);
    this._onSearch = this._onSearch.bind(this);
    this._onScroll = this._onScroll.bind(this);
    this._onExpand = this._onExpand.bind(this);
    this._onStackFileClick = this._onStackFileClick.bind(this);
    this._onThumbnailClick = this._onThumbnailClick.bind(this);

    this.widget.addEventListener("select", this._onSelect);
    this._searchbox.addEventListener("input", this._onSearch);
    this._filmstrip.addEventListener("wheel", this._onScroll);
  },

  /**
   * Destruction function, called when the tool is closed.
   */
  destroy: function() {
    this.widget.removeEventListener("select", this._onSelect);
    this._searchbox.removeEventListener("input", this._onSearch);
    this._filmstrip.removeEventListener("wheel", this._onScroll);
  },

  /**
   * Populates this container with a list of function calls.
   *
   * @param array functionCalls
   *        A list of function call actors received from the backend.
   */
  showCalls: function(functionCalls) {
    this.empty();

    for (let i = 0, len = functionCalls.length; i < len; i++) {
      const call = functionCalls[i];

      const view = document.createElement("vbox");
      view.className = "call-item-view devtools-monospace";
      view.setAttribute("flex", "1");

      const contents = document.createElement("hbox");
      contents.className = "call-item-contents";
      contents.setAttribute("align", "center");
      contents.addEventListener("dblclick", this._onExpand);
      view.appendChild(contents);

      const index = document.createElement("label");
      index.className = "plain call-item-index";
      index.setAttribute("flex", "1");
      index.setAttribute("value", i + 1);

      const gutter = document.createElement("hbox");
      gutter.className = "call-item-gutter";
      gutter.appendChild(index);
      contents.appendChild(gutter);

      if (call.callerPreview) {
        const context = document.createElement("label");
        context.className = "plain call-item-context";
        context.setAttribute("value", call.callerPreview);
        contents.appendChild(context);

        const separator = document.createElement("label");
        separator.className = "plain call-item-separator";
        separator.setAttribute("value", ".");
        contents.appendChild(separator);
      }

      const name = document.createElement("label");
      name.className = "plain call-item-name";
      name.setAttribute("value", call.name);
      contents.appendChild(name);

      const argsPreview = document.createElement("label");
      argsPreview.className = "plain call-item-args";
      argsPreview.setAttribute("crop", "end");
      argsPreview.setAttribute("flex", "100");
      // Getters and setters are displayed differently from regular methods.
      if (call.type == CallWatcherFront.METHOD_FUNCTION) {
        argsPreview.setAttribute("value", "(" + call.argsPreview + ")");
      } else {
        argsPreview.setAttribute("value", " = " + call.argsPreview);
      }
      contents.appendChild(argsPreview);

      const location = document.createElement("label");
      location.className = "plain call-item-location";
      location.setAttribute("value", getFileName(call.file) + ":" + call.line);
      location.setAttribute("crop", "start");
      location.setAttribute("flex", "1");
      location.addEventListener("mousedown", this._onExpand);
      contents.appendChild(location);

      // Append a function call item to this container.
      this.push([view], {
        staged: true,
        attachment: {
          actor: call
        }
      });

      // Highlight certain calls that are probably more interesting than
      // everything else, making it easier to quickly glance over them.
      if (CanvasFront.DRAW_CALLS.has(call.name)) {
        view.setAttribute("draw-call", "");
      }
      if (CanvasFront.INTERESTING_CALLS.has(call.name)) {
        view.setAttribute("interesting-call", "");
      }
    }

    // Flushes all the prepared function call items into this container.
    this.commit();
    window.emit(EVENTS.CALL_LIST_POPULATED);
  },

  /**
   * Displays an image in the rendering preview of this container, generated
   * for the specified draw call in the recorded animation frame snapshot.
   *
   * @param array screenshot
   *        A single "snapshot-image" instance received from the backend.
   */
  showScreenshot: function(screenshot) {
    const { index, width, height, scaling, flipped, pixels } = screenshot;

    const screenshotNode = $("#screenshot-image");
    screenshotNode.setAttribute("flipped", flipped);
    drawBackground("screenshot-rendering", width, height, pixels);

    const dimensionsNode = $("#screenshot-dimensions");
    const actualWidth = (width / scaling) | 0;
    const actualHeight = (height / scaling) | 0;
    dimensionsNode.setAttribute("value",
      SHARED_L10N.getFormatStr("dimensions", actualWidth, actualHeight));

    window.emit(EVENTS.CALL_SCREENSHOT_DISPLAYED);
  },

  /**
   * Populates this container's footer with a list of thumbnails, one generated
   * for each draw call in the recorded animation frame snapshot.
   *
   * @param array thumbnails
   *        An array of "snapshot-image" instances received from the backend.
   */
  showThumbnails: function(thumbnails) {
    while (this._filmstrip.hasChildNodes()) {
      this._filmstrip.firstChild.remove();
    }
    for (const thumbnail of thumbnails) {
      this.appendThumbnail(thumbnail);
    }

    window.emit(EVENTS.THUMBNAILS_DISPLAYED);
  },

  /**
   * Displays an image in the thumbnails list of this container, generated
   * for the specified draw call in the recorded animation frame snapshot.
   *
   * @param array thumbnail
   *        A single "snapshot-image" instance received from the backend.
   */
  appendThumbnail: function(thumbnail) {
    const { index, width, height, flipped, pixels } = thumbnail;

    const thumbnailNode = document.createElementNS(HTML_NS, "canvas");
    thumbnailNode.setAttribute("flipped", flipped);
    thumbnailNode.width = Math.max(CanvasFront.THUMBNAIL_SIZE, width);
    thumbnailNode.height = Math.max(CanvasFront.THUMBNAIL_SIZE, height);
    drawImage(thumbnailNode, width, height, pixels, { centered: true });

    thumbnailNode.className = "filmstrip-thumbnail";
    thumbnailNode.onmousedown = e => this._onThumbnailClick(e, index);
    thumbnailNode.setAttribute("index", index);
    this._filmstrip.appendChild(thumbnailNode);
  },

  /**
   * Sets the currently highlighted thumbnail in this container.
   * A screenshot will always correlate to a thumbnail in the filmstrip,
   * both being identified by the same 'index' of the context function call.
   *
   * @param number index
   *        The context function call's index.
   */
  set highlightedThumbnail(index) {
    const currHighlightedThumbnail = $(".filmstrip-thumbnail[index='" + index + "']");
    if (currHighlightedThumbnail == null) {
      return;
    }

    const prevIndex = this._highlightedThumbnailIndex;
    const prevHighlightedThumbnail = $(".filmstrip-thumbnail[index='" + prevIndex + "']");
    if (prevHighlightedThumbnail) {
      prevHighlightedThumbnail.removeAttribute("highlighted");
    }

    currHighlightedThumbnail.setAttribute("highlighted", "");
    currHighlightedThumbnail.scrollIntoView();
    this._highlightedThumbnailIndex = index;
  },

  /**
   * Gets the currently highlighted thumbnail in this container.
   * @return number
   */
  get highlightedThumbnail() {
    return this._highlightedThumbnailIndex;
  },

  /**
   * The select listener for this container.
   */
  _onSelect: function({ detail: callItem }) {
    if (!callItem) {
      return;
    }

    // Some of the stepping buttons don't make sense specifically while the
    // last function call is selected.
    if (this.selectedIndex == this.itemCount - 1) {
      $("#resume").setAttribute("disabled", "true");
      $("#step-over").setAttribute("disabled", "true");
      $("#step-out").setAttribute("disabled", "true");
    } else {
      $("#resume").removeAttribute("disabled");
      $("#step-over").removeAttribute("disabled");
      $("#step-out").removeAttribute("disabled");
    }

    // Can't generate screenshots for function call actors loaded from disk.
    // XXX: Bug 984844.
    if (callItem.attachment.actor.isLoadedFromDisk) {
      return;
    }

    // To keep continuous selection buttery smooth (for example, while pressing
    // the DOWN key or moving the slider), only display the screenshot after
    // any kind of user input stops.
    setConditionalTimeout("screenshot-display", SCREENSHOT_DISPLAY_DELAY, () => {
      return !this._isSliding;
    }, () => {
      const frameSnapshot = SnapshotsListView.selectedItem.attachment.actor;
      const functionCall = callItem.attachment.actor;
      frameSnapshot.generateScreenshotFor(functionCall).then(screenshot => {
        this.showScreenshot(screenshot);
        this.highlightedThumbnail = screenshot.index;
      }).catch(console.error);
    });
  },

  /**
   * The input listener for the calls searchbox.
   */
  _onSearch: function(e) {
    const lowerCaseSearchToken = this._searchbox.value.toLowerCase();

    this.filterContents(e => {
      const call = e.attachment.actor;
      const name = call.name.toLowerCase();
      const file = call.file.toLowerCase();
      const line = call.line.toString().toLowerCase();
      const args = call.argsPreview.toLowerCase();

      return name.includes(lowerCaseSearchToken) ||
             file.includes(lowerCaseSearchToken) ||
             line.includes(lowerCaseSearchToken) ||
             args.includes(lowerCaseSearchToken);
    });
  },

  /**
   * The wheel listener for the filmstrip that contains all the thumbnails.
   */
  _onScroll: function(e) {
    this._filmstrip.scrollLeft += e.deltaX;
  },

  /**
   * The click/dblclick listener for an item or location url in this container.
   * When expanding an item, it's corresponding call stack will be displayed.
   */
  _onExpand: function(e) {
    const callItem = this.getItemForElement(e.target);
    const view = $(".call-item-view", callItem.target);

    // If the call stack nodes were already created, simply re-show them
    // or jump to the corresponding file and line in the Debugger if a
    // location link was clicked.
    if (view.hasAttribute("call-stack-populated")) {
      const isExpanded = view.getAttribute("call-stack-expanded") == "true";

      // If clicking on the location, jump to the Debugger.
      if (e.target.classList.contains("call-item-location")) {
        const { file, line } = callItem.attachment.actor;
        this._viewSourceInDebugger(file, line);
        return;
      }
      // Otherwise hide the call stack.

      view.setAttribute("call-stack-expanded", !isExpanded);
      $(".call-item-stack", view).hidden = isExpanded;
      return;
    }

    const list = document.createElement("vbox");
    list.className = "call-item-stack";
    view.setAttribute("call-stack-populated", "");
    view.setAttribute("call-stack-expanded", "true");
    view.appendChild(list);

    /**
     * Creates a function call nodes in this container for a stack.
     */
    const display = stack => {
      for (let i = 1; i < stack.length; i++) {
        const call = stack[i];

        const contents = document.createElement("hbox");
        contents.className = "call-item-stack-fn";
        contents.style.paddingInlineStart = (i * STACK_FUNC_INDENTATION) + "px";

        const name = document.createElement("label");
        name.className = "plain call-item-stack-fn-name";
        name.setAttribute("value", "↳ " + call.name + "()");
        contents.appendChild(name);

        const spacer = document.createElement("spacer");
        spacer.setAttribute("flex", "100");
        contents.appendChild(spacer);

        const location = document.createElement("label");
        location.className = "plain call-item-stack-fn-location";
        location.setAttribute("value", getFileName(call.file) + ":" + call.line);
        location.setAttribute("crop", "start");
        location.setAttribute("flex", "1");
        location.addEventListener("mousedown", e => this._onStackFileClick(e, call));
        contents.appendChild(location);

        list.appendChild(contents);
      }

      window.emit(EVENTS.CALL_STACK_DISPLAYED);
    };

    // If this animation snapshot is loaded from disk, there are no corresponding
    // backend actors available and the data is immediately available.
    const functionCall = callItem.attachment.actor;
    if (functionCall.isLoadedFromDisk) {
      display(functionCall.stack);
    } else {
      // ..otherwise we need to request the function call stack from the backend.
      callItem.attachment.actor.getDetails().then(fn => display(fn.stack));
    }
  },

  /**
   * The click listener for a location link in the call stack.
   *
   * @param string file
   *        The url of the source owning the function.
   * @param number line
   *        The line of the respective function.
   */
  _onStackFileClick: function(e, { file, line }) {
    this._viewSourceInDebugger(file, line);
  },

  /**
   * The click listener for a thumbnail in the filmstrip.
   *
   * @param number index
   *        The function index in the recorded animation frame snapshot.
   */
  _onThumbnailClick: function(e, index) {
    this.selectedIndex = index;
  },

  /**
   * The click listener for the "resume" button in this container's toolbar.
   */
  _onResume: function() {
    // Jump to the next draw call in the recorded animation frame snapshot.
    const drawCall = getNextDrawCall(this.items, this.selectedItem);
    if (drawCall) {
      this.selectedItem = drawCall;
      return;
    }

    // If there are no more draw calls, just jump to the last context call.
    this._onStepOut();
  },

  /**
   * The click listener for the "step over" button in this container's toolbar.
   */
  _onStepOver: function() {
    this.selectedIndex++;
  },

  /**
   * The click listener for the "step in" button in this container's toolbar.
   */
  _onStepIn: function() {
    if (this.selectedIndex == -1) {
      this._onResume();
      return;
    }
    const callItem = this.selectedItem;
    const { file, line } = callItem.attachment.actor;
    this._viewSourceInDebugger(file, line);
  },

  /**
   * The click listener for the "step out" button in this container's toolbar.
   */
  _onStepOut: function() {
    this.selectedIndex = this.itemCount - 1;
  },

  /**
   * Opens the specified file and line in the debugger. Falls back to Firefox's View Source.
   */
  _viewSourceInDebugger: function(file, line) {
    gToolbox.viewSourceInDebugger(file, line).then(success => {
      if (success) {
        window.emit(EVENTS.SOURCE_SHOWN_IN_JS_DEBUGGER);
      } else {
        window.emit(EVENTS.SOURCE_NOT_FOUND_IN_JS_DEBUGGER);
      }
    });
  }
});
