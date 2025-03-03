/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {require} = ChromeUtils.import("resource://devtools/shared/Loader.jsm", {});
const {TimeScale} = require("devtools/client/inspector/animation-old/utils");
const TEST_ANIMATIONS = [{
  desc: "Testing a few standard animations",
  animations: [{
    previousStartTime: 500,
    delay: 0,
    duration: 1000,
    iterationCount: 1,
    playbackRate: 1
  }, {
    previousStartTime: 400,
    delay: 100,
    duration: 10,
    iterationCount: 100,
    playbackRate: 1
  }, {
    previousStartTime: 50,
    delay: 1000,
    duration: 100,
    iterationCount: 20,
    playbackRate: 1
  }],
  expectedMinStart: 50,
  expectedMaxEnd: 3050
}, {
  desc: "Testing a single negative-delay animation",
  animations: [{
    previousStartTime: 100,
    delay: -100,
    duration: 100,
    iterationCount: 1,
    playbackRate: 1
  }],
  expectedMinStart: 0,
  expectedMaxEnd: 100
}, {
  desc: "Testing a single negative-delay animation with a different rate",
  animations: [{
    previousStartTime: 3500,
    delay: -1000,
    duration: 10000,
    iterationCount: 2,
    playbackRate: 2
  }],
  expectedMinStart: 3000,
  expectedMaxEnd: 13000
}];

const TEST_STARTTIME_TO_DISTANCE = [{
  time: 50,
  expectedDistance: 0
}, {
  time: 50,
  expectedDistance: 0
}, {
  time: 3050,
  expectedDistance: 100
}, {
  time: 1550,
  expectedDistance: 50
}];

const TEST_DURATION_TO_DISTANCE = [{
  time: 3000,
  expectedDistance: 100
}, {
  time: 0,
  expectedDistance: 0
}];

const TEST_DISTANCE_TO_TIME = [{
  distance: 100,
  expectedTime: 3050
}, {
  distance: 0,
  expectedTime: 50
}, {
  distance: 25,
  expectedTime: 800
}];

const TEST_DISTANCE_TO_RELATIVE_TIME = [{
  distance: 100,
  expectedTime: 3000
}, {
  distance: 0,
  expectedTime: 0
}, {
  distance: 25,
  expectedTime: 750
}];

const TEST_FORMAT_TIME_MS = [{
  time: 0,
  expectedFormattedTime: "0ms"
}, {
  time: 3540.341,
  expectedFormattedTime: "3540ms"
}, {
  time: 1.99,
  expectedFormattedTime: "2ms"
}, {
  time: 4000,
  expectedFormattedTime: "4000ms"
}];

const TEST_FORMAT_TIME_S = [{
  time: 0,
  expectedFormattedTime: "0.0s"
}, {
  time: 3540.341,
  expectedFormattedTime: "3.5s"
}, {
  time: 1.99,
  expectedFormattedTime: "0.0s"
}, {
  time: 4000,
  expectedFormattedTime: "4.0s"
}, {
  time: 102540,
  expectedFormattedTime: "102.5s"
}, {
  time: 102940,
  expectedFormattedTime: "102.9s"
}];

function run_test() {
  info("Check the default min/max range values");
  equal(TimeScale.minStartTime, Infinity);
  equal(TimeScale.maxEndTime, 0);

  for (const {desc, animations, expectedMinStart, expectedMaxEnd} of
       TEST_ANIMATIONS) {
    info("Test adding a few animations: " + desc);
    for (const state of animations) {
      TimeScale.addAnimation(state);
    }

    info("Checking the time scale range");
    equal(TimeScale.minStartTime, expectedMinStart);
    equal(TimeScale.maxEndTime, expectedMaxEnd);

    info("Test reseting the animations");
    TimeScale.reset();
    equal(TimeScale.minStartTime, Infinity);
    equal(TimeScale.maxEndTime, 0);
  }

  info("Add a set of animations again");
  for (const state of TEST_ANIMATIONS[0].animations) {
    TimeScale.addAnimation(state);
  }

  info("Test converting start times to distances");
  for (const {time, expectedDistance} of TEST_STARTTIME_TO_DISTANCE) {
    const distance = TimeScale.startTimeToDistance(time);
    equal(distance, expectedDistance);
  }

  info("Test converting durations to distances");
  for (const {time, expectedDistance} of TEST_DURATION_TO_DISTANCE) {
    const distance = TimeScale.durationToDistance(time);
    equal(distance, expectedDistance);
  }

  info("Test converting distances to times");
  for (const {distance, expectedTime} of TEST_DISTANCE_TO_TIME) {
    const time = TimeScale.distanceToTime(distance);
    equal(time, expectedTime);
  }

  info("Test converting distances to relative times");
  for (const {distance, expectedTime} of TEST_DISTANCE_TO_RELATIVE_TIME) {
    const time = TimeScale.distanceToRelativeTime(distance);
    equal(time, expectedTime);
  }

  info("Test formatting times (millis)");
  for (const {time, expectedFormattedTime} of TEST_FORMAT_TIME_MS) {
    const formattedTime = TimeScale.formatTime(time);
    equal(formattedTime, expectedFormattedTime);
  }

  // Add 1 more animation to increase the range and test more time formatting
  // cases.
  TimeScale.addAnimation({
    startTime: 3000,
    duration: 5000,
    delay: 0,
    iterationCount: 1
  });

  info("Test formatting times (seconds)");
  for (const {time, expectedFormattedTime} of TEST_FORMAT_TIME_S) {
    const formattedTime = TimeScale.formatTime(time);
    equal(formattedTime, expectedFormattedTime);
  }
}
