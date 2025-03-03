/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("devtools/shared/event-emitter");
const XHTML_NS = "http://www.w3.org/1999/xhtml";

/**
 * Spectrum creates a color picker widget in any container you give it.
 *
 * Simple usage example:
 *
 * const {Spectrum} = require("devtools/client/shared/widgets/Spectrum");
 * let s = new Spectrum(containerElement, [255, 126, 255, 1]);
 * s.on("changed", (rgba, color) => {
 *   console.log("rgba(" + rgba[0] + ", " + rgba[1] + ", " + rgba[2] + ", " +
 *     rgba[3] + ")");
 * });
 * s.show();
 * s.destroy();
 *
 * Note that the color picker is hidden by default and you need to call show to
 * make it appear. This 2 stages initialization helps in cases you are creating
 * the color picker in a parent element that hasn't been appended anywhere yet
 * or that is hidden. Calling show() when the parent element is appended and
 * visible will allow spectrum to correctly initialize its various parts.
 *
 * Fires the following events:
 * - changed : When the user changes the current color
 */
function Spectrum(parentEl, rgb) {
  EventEmitter.decorate(this);

  this.element = parentEl.ownerDocument.createElementNS(XHTML_NS, "div");
  this.parentEl = parentEl;

  this.element.className = "spectrum-container";
  this.element.innerHTML = `
    <div class="spectrum-top">
      <div class="spectrum-fill"></div>
      <div class="spectrum-top-inner">
        <div class="spectrum-color spectrum-box">
          <div class="spectrum-sat">
            <div class="spectrum-val">
              <div class="spectrum-dragger"></div>
            </div>
          </div>
        </div>
        <div class="spectrum-hue spectrum-box">
          <div class="spectrum-slider spectrum-slider-control"></div>
        </div>
      </div>
    </div>
    <div class="spectrum-alpha spectrum-checker spectrum-box">
      <div class="spectrum-alpha-inner">
        <div class="spectrum-alpha-handle spectrum-slider-control"></div>
      </div>
    </div>
  `;

  this.onElementClick = this.onElementClick.bind(this);
  this.element.addEventListener("click", this.onElementClick);

  this.parentEl.appendChild(this.element);

  this.slider = this.element.querySelector(".spectrum-hue");
  this.slideHelper = this.element.querySelector(".spectrum-slider");
  Spectrum.draggable(this.slider, this.onSliderMove.bind(this));

  this.dragger = this.element.querySelector(".spectrum-color");
  this.dragHelper = this.element.querySelector(".spectrum-dragger");
  Spectrum.draggable(this.dragger, this.onDraggerMove.bind(this));

  this.alphaSlider = this.element.querySelector(".spectrum-alpha");
  this.alphaSliderInner = this.element.querySelector(".spectrum-alpha-inner");
  this.alphaSliderHelper = this.element.querySelector(".spectrum-alpha-handle");
  Spectrum.draggable(this.alphaSliderInner, this.onAlphaSliderMove.bind(this));

  if (rgb) {
    this.rgb = rgb;
    this.updateUI();
  }
}

module.exports.Spectrum = Spectrum;

Spectrum.hsvToRgb = function(h, s, v, a) {
  let r, g, b;

  const i = Math.floor(h * 6);
  const f = h * 6 - i;
  const p = v * (1 - s);
  const q = v * (1 - f * s);
  const t = v * (1 - (1 - f) * s);

  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }

  return [r * 255, g * 255, b * 255, a];
};

Spectrum.rgbToHsv = function(r, g, b, a) {
  r = r / 255;
  g = g / 255;
  b = b / 255;

  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);

  const v = max;
  const d = max - min;
  const s = max == 0 ? 0 : d / max;

  let h;
  if (max == min) {
    // achromatic
    h = 0;
  } else {
    switch (max) {
      case r: h = (g - b) / d + (g < b ? 6 : 0); break;
      case g: h = (b - r) / d + 2; break;
      case b: h = (r - g) / d + 4; break;
    }
    h /= 6;
  }
  return [h, s, v, a];
};

Spectrum.draggable = function(element, onmove, onstart, onstop) {
  onmove = onmove || function() {};
  onstart = onstart || function() {};
  onstop = onstop || function() {};

  const doc = element.ownerDocument;
  let dragging = false;
  let offset = {};
  let maxHeight = 0;
  let maxWidth = 0;

  function prevent(e) {
    e.stopPropagation();
    e.preventDefault();
  }

  function move(e) {
    if (dragging) {
      if (e.buttons === 0) {
        // The button is no longer pressed but we did not get a mouseup event.
        stop();
        return;
      }
      const pageX = e.pageX;
      const pageY = e.pageY;

      const dragX = Math.max(0, Math.min(pageX - offset.left, maxWidth));
      const dragY = Math.max(0, Math.min(pageY - offset.top, maxHeight));

      onmove.apply(element, [dragX, dragY]);
    }
  }

  function start(e) {
    const rightclick = e.which === 3;

    if (!rightclick && !dragging) {
      if (onstart.apply(element, arguments) !== false) {
        dragging = true;
        maxHeight = element.offsetHeight;
        maxWidth = element.offsetWidth;

        offset = element.getBoundingClientRect();

        move(e);

        doc.addEventListener("selectstart", prevent);
        doc.addEventListener("dragstart", prevent);
        doc.addEventListener("mousemove", move);
        doc.addEventListener("mouseup", stop);

        prevent(e);
      }
    }
  }

  function stop() {
    if (dragging) {
      doc.removeEventListener("selectstart", prevent);
      doc.removeEventListener("dragstart", prevent);
      doc.removeEventListener("mousemove", move);
      doc.removeEventListener("mouseup", stop);
      onstop.apply(element, arguments);
    }
    dragging = false;
  }

  element.addEventListener("mousedown", start);
};

Spectrum.prototype = {
  set rgb(color) {
    this.hsv = Spectrum.rgbToHsv(color[0], color[1], color[2], color[3]);
  },

  get rgb() {
    const rgb = Spectrum.hsvToRgb(this.hsv[0], this.hsv[1], this.hsv[2],
      this.hsv[3]);
    return [Math.round(rgb[0]), Math.round(rgb[1]), Math.round(rgb[2]),
            Math.round(rgb[3] * 100) / 100];
  },

  get rgbNoSatVal() {
    const rgb = Spectrum.hsvToRgb(this.hsv[0], 1, 1);
    return [Math.round(rgb[0]), Math.round(rgb[1]), Math.round(rgb[2]), rgb[3]];
  },

  get rgbCssString() {
    const rgb = this.rgb;
    return "rgba(" + rgb[0] + ", " + rgb[1] + ", " + rgb[2] + ", " +
      rgb[3] + ")";
  },

  show: function() {
    this.element.classList.add("spectrum-show");

    this.slideHeight = this.slider.offsetHeight;
    this.dragWidth = this.dragger.offsetWidth;
    this.dragHeight = this.dragger.offsetHeight;
    this.dragHelperHeight = this.dragHelper.offsetHeight;
    this.slideHelperHeight = this.slideHelper.offsetHeight;
    this.alphaSliderWidth = this.alphaSliderInner.offsetWidth;
    this.alphaSliderHelperWidth = this.alphaSliderHelper.offsetWidth;

    this.updateUI();
  },

  onElementClick: function(e) {
    e.stopPropagation();
  },

  onSliderMove: function(dragX, dragY) {
    this.hsv[0] = (dragY / this.slideHeight);
    this.updateUI();
    this.onChange();
  },

  onDraggerMove: function(dragX, dragY) {
    this.hsv[1] = dragX / this.dragWidth;
    this.hsv[2] = (this.dragHeight - dragY) / this.dragHeight;
    this.updateUI();
    this.onChange();
  },

  onAlphaSliderMove: function(dragX, dragY) {
    this.hsv[3] = dragX / this.alphaSliderWidth;
    this.updateUI();
    this.onChange();
  },

  onChange: function() {
    this.emit("changed", this.rgb, this.rgbCssString);
  },

  updateHelperLocations: function() {
    // If the UI hasn't been shown yet then none of the dimensions will be
    // correct
    if (!this.element.classList.contains("spectrum-show")) {
      return;
    }

    const h = this.hsv[0];
    const s = this.hsv[1];
    const v = this.hsv[2];

    // Placing the color dragger
    let dragX = s * this.dragWidth;
    let dragY = this.dragHeight - (v * this.dragHeight);
    const helperDim = this.dragHelperHeight / 2;

    dragX = Math.max(
      -helperDim,
      Math.min(this.dragWidth - helperDim, dragX - helperDim)
    );
    dragY = Math.max(
      -helperDim,
      Math.min(this.dragHeight - helperDim, dragY - helperDim)
    );

    this.dragHelper.style.top = dragY + "px";
    this.dragHelper.style.left = dragX + "px";

    // Placing the hue slider
    const slideY = (h * this.slideHeight) - this.slideHelperHeight / 2;
    this.slideHelper.style.top = slideY + "px";

    // Placing the alpha slider
    const alphaSliderX = (this.hsv[3] * this.alphaSliderWidth) -
      (this.alphaSliderHelperWidth / 2);
    this.alphaSliderHelper.style.left = alphaSliderX + "px";
  },

  updateUI: function() {
    this.updateHelperLocations();

    const rgb = this.rgb;
    const rgbNoSatVal = this.rgbNoSatVal;

    const flatColor = "rgb(" + rgbNoSatVal[0] + ", " + rgbNoSatVal[1] + ", " +
      rgbNoSatVal[2] + ")";

    this.dragger.style.backgroundColor = flatColor;

    const rgbNoAlpha = "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")";
    const rgbAlpha0 = "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ", 0)";
    const alphaGradient = "linear-gradient(to right, " + rgbAlpha0 + ", " +
      rgbNoAlpha + ")";
    this.alphaSliderInner.style.background = alphaGradient;
  },

  destroy: function() {
    this.element.removeEventListener("click", this.onElementClick);

    this.parentEl.removeChild(this.element);

    this.slider = null;
    this.dragger = null;
    this.alphaSlider = this.alphaSliderInner = this.alphaSliderHelper = null;
    this.parentEl = null;
    this.element = null;
  }
};
