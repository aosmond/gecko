/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the autocomplete popup is resized when needed.

const TEST_URI = `data:text/html;charset=utf-8,
<head>
  <script>
    /* Create prototype-less object so popup does not contain native
     * Object prototype properties.
     */
    window.x = Object.create(null, Object.getOwnPropertyDescriptors({
      ["y".repeat(10)]: 1,
      ["z".repeat(20)]: 2
    }));
    window.xx = 1;
  </script>
</head>
<body>Test</body>`;

add_task(async function() {
  // Run test with legacy JsTerm
  await performTests();
  // And then run it with the CodeMirror-powered one.
  await pushPref("devtools.webconsole.jsterm.codeMirror", true);
  await performTests();
});

async function performTests() {
  const { jsterm } = await openNewTabAndConsole(TEST_URI);
  const { autocompletePopup: popup } = jsterm;

  const onPopUpOpen = popup.once("popup-opened");

  info(`wait for completion suggestions for "x"`);
  EventUtils.sendString("x");

  await onPopUpOpen;

  ok(popup.isOpen, "popup is open");

  const expectedPopupItems = ["x", "xx"];
  is(popup.items.map(i => i.label).join("-"), expectedPopupItems.join("-"),
    "popup has expected items");

  const originalWidth = popup._tooltip.container.clientWidth;
  ok(originalWidth > 2 * jsterm._inputCharWidth,
    "popup is at least wider than the width of the longest list item");

  info(`wait for completion suggestions for "x."`);
  let onAutocompleteUpdated = jsterm.once("autocomplete-updated");
  EventUtils.sendString(".");
  await onAutocompleteUpdated;

  is(popup.items.map(i => i.label).join("-"), ["y".repeat(10), "z".repeat(20)].join("-"),
    "popup has expected items");
  const newPopupWidth = popup._tooltip.container.clientWidth;
  ok(newPopupWidth > originalWidth, "The popup width was updated");
  ok(newPopupWidth > 20 * jsterm._inputCharWidth,
    "popup is at least wider than the width of the longest list item");

  info(`wait for completion suggestions for "x"`);
  onAutocompleteUpdated = jsterm.once("autocomplete-updated");
  EventUtils.synthesizeKey("KEY_Backspace");
  await onAutocompleteUpdated;

  is(popup._tooltip.container.clientWidth, originalWidth,
    "popup is back to its original width");
}
