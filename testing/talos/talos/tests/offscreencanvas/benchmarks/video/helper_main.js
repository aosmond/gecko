/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*global runTestInternal b:true*/

async function runTest(testName, canvasType, videoUri) {
  const canvas = document.createElement("canvas");
  canvas.width = 1920;
  canvas.height = 1080;
  document.body.appendChild(canvas);

  const offscreenCanvas = canvas.transferControlToOffscreen();

  const result = await new Promise((resolve, reject) => {
    try {
      runTestInternal(
        testName,
        canvasType,
        offscreenCanvas,
        /* isWorker */ false,
        videoUri,
        resolve,
        reject
      );
    } catch (e) {
      reject(e);
    }
  });

  let name =
    result.testName + " Mean time across " + result.totalFrames + " frames: ";
  let value = result.elapsed / result.totalFrames;
  let msg = name + value + "\n";
  dump("[talos offscreen-canvas-webcodecs result] " + msg);

  if (window.tpRecordTime) {
    // Within talos - report the results
    tpRecordTime(value, 0, name);
  } else {
    alert(msg);
  }

  return Promise.resolve(result);
}
