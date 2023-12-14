/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

async function runTest(testName, canvasType, worker, videoUri) {
  const canvas = document.createElement("canvas");
  canvas.width = 1920;
  canvas.height = 1080;
  document.body.appendChild(canvas);

  const offscreenCanvas = canvas.transferControlToOffscreen();

  const result = await new Promise((resolve, reject) => {
    worker.onmessage = e => {
      if (e.data.errorMessage) {
        reject(e.data.errorMessage);
      } else {
        resolve(e.data);
      }
    };

    worker.postMessage({ offscreenCanvas, testName, canvasType, videoUri }, [
      offscreenCanvas,
    ]);
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
