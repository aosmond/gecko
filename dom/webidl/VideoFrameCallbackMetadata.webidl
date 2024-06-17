/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * TODO: COMPLETE
 */

[Exposed=Window]
interface VideoFrameCallbackMetadata {
  readonly attribute DOMHighResTimeStamp presentationTime;
  readonly attribute DOMHighResTimeStamp expectedDisplayTime;

  readonly attribute unsigned long width;
  readonly attribute unsigned long height;
  readonly attribute double mediaTime;

  readonly attribute unsigned long presentedFrames;
  readonly attribute double processingDuration;

  readonly attribute DOMHighResTimeStamp captureTime;
  readonly attribute DOMHighResTimeStamp receiveTime;
  readonly attribute unsigned long rtpTimestamp;
};
