// |reftest| skip-if(!this.hasOwnProperty('Atomics')) -- Atomics is not enabled unconditionally
// Copyright (C) 2017 Mozilla Corporation. All rights reserved.
// This code is governed by the license found in the LICENSE file.

/*---
esid: sec-atomics.wake
description: Testing descriptor property of Atomics.wake
includes: [propertyHelper.js]
features: [Atomics]
---*/

verifyProperty(Atomics, 'wake', {
  enumerable: false,
  writable: true,
  configurable: true,
});

reportCompare(0, 0);
