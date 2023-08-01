// META: global=window,dedicatedworker
// META: script=/webcodecs/image-decoder-utils.js

function testFourColorsDecode(filename, mimeType, options = {}) {
  var decoder = null;
  return ImageDecoder.isTypeSupported(mimeType).then(support => {
    assert_implements_optional(
        support, 'Optional codec ' + mimeType + ' not supported.');
    return fetch(filename).then(response => {
      return testFourColorsDecodeBuffer(response.body, mimeType, options);
    });
  });
}

// Note: Requiring all data to do YUV decoding is a Chromium limitation, other
// implementations may support YUV decode with partial ReadableStream data.
function testFourColorsYuvDecode(filename, mimeType, options = {}) {
  var decoder = null;
  return ImageDecoder.isTypeSupported(mimeType).then(support => {
    assert_implements_optional(
        support, 'Optional codec ' + mimeType + ' not supported.');
    return fetch(filename).then(response => {
      return response.arrayBuffer().then(buffer => {
        return testFourColorsDecodeBuffer(buffer, mimeType, options);
      });
    });
  });
}

class InfiniteGifSource {
  async load(repetitionCount) {
    let response = await fetch('four-colors-flip.gif');
    let buffer = await response.arrayBuffer();

    // Strip GIF trailer (0x3B) so we can continue to append frames.
    this.baseImage = new Uint8Array(buffer.slice(0, buffer.byteLength - 1));
    this.baseImage[0x23] = repetitionCount;
    this.counter = 0;
  }

  start(controller) {
    this.controller = controller;
    this.controller.enqueue(this.baseImage);
  }

  close() {
    this.controller.enqueue(new Uint8Array([0x3B]));
    this.controller.close();
  }

  addFrame() {
    const FRAME1_START = 0x26;
    const FRAME2_START = 0x553;

    if (this.counter++ % 2 == 0)
      this.controller.enqueue(this.baseImage.slice(FRAME1_START, FRAME2_START));
    else
      this.controller.enqueue(this.baseImage.slice(FRAME2_START));
  }
}

promise_test(async t => {
  let support = await ImageDecoder.isTypeSupported('image/gif');
  assert_implements_optional(
      support, 'Optional codec image/gif not supported.');

  let source = new InfiniteGifSource();
  await source.load(5);

  let stream = new ReadableStream(source, {type: 'bytes'});
  let decoder = new ImageDecoder({data: stream, type: 'image/gif'});
  return decoder.tracks.ready.then(_ => {
    assert_equals(decoder.tracks.selectedTrack.frameCount, 2);
    assert_equals(decoder.tracks.selectedTrack.repetitionCount, 5);

    // Decode frame not yet available then change tracks before it comes in.
    let p = decoder.decode({frameIndex: 5});
    decoder.tracks.selectedTrack.selected = false;
    return promise_rejects_dom(t, 'AbortError', p);
  });
}, 'Test ReadableStream aborts promises on track change');
