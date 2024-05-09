# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import filters
from base_python_support import BasePythonSupport


class VideoPlaybackLatency(BasePythonSupport):
    def modify_command(self, cmd, test):
        cmd += [
            "--visualMetricsKeyColor",
            "poster",
            "120",
            "250",
            "250",
            "20",
            "0.75",
            "--visualMetricsKeyColor",
            "firstFrame",
            "250",
            "20",
            "20",
            "20",
            "0.75",
            "--visualMetricsKeyColor",
            "secondFrame",
            "20",
            "20",
            "250",
            "20",
            "0.75",
            "--visualMetricsKeyColor",
            "lastFrame",
            "255",
            "255",
            "50",
            "50",
            "0.75",
            "--chrome.enableVideoAutoplay",
            "true",
            "--videoParams.framerate",
            "60",
        ]

    def handle_result(self, bt_result, raw_result, last_result=False, **kwargs):
        measurements = {
            "poster": [],
            "firstFrame": [],
            "secondFrame": [],
            "lastFrame": [],
            "estimatedFirstFrameLatency": [],
            "estimatedAnyFrameLatency": [],
        }

        fps = 30.0
        total_duration_ms = 1000.0
        frame_duration_ms = total_duration_ms / fps

        offsets = {
            "firstFrame": 0.0,
            "secondFrame": frame_duration_ms,
            "lastFrame": total_duration_ms - frame_duration_ms,
        }

        # Gather the key frame start times of each page/cycle
        for cycle in raw_result["visualMetrics"]:
            measurement = {}
            for key, frames in cycle["KeyColorFrames"].items():
                if key not in measurements or not len(frames):
                    continue
                measurement[key] = frames[0]["startTimestamp"]

            for key in ["firstFrame", "secondFrame", "lastFrame"]:
                if key in measurement:
                    normalized_value = measurement[key] - offsets[key]
                    if normalized_value > 0:
                        measurements["estimatedFirstFrameLatency"].append(
                            normalized_value
                        )
                        break

            for key, value in measurement.items():
                measurements[key].append(value)
                if key in offsets:
                    normalized_value = value - offsets[key]
                    if normalized_value > 0:
                        measurements["estimatedAnyFrameLatency"].append(
                            normalized_value
                        )

        for measurement, values in measurements.items():
            bt_result["measurements"].setdefault(measurement, []).extend(values)

    def _build_subtest(self, measurement_name, replicates, test):
        unit = test.get("unit", "ms")
        if test.get("subtest_unit"):
            unit = test.get("subtest_unit")

        return {
            "name": measurement_name,
            "lowerIsBetter": test.get("lower_is_better", True),
            "alertThreshold": float(test.get("alert_threshold", 2.0)),
            "unit": unit,
            "replicates": replicates,
            "value": round(filters.geometric_mean(replicates), 3),
        }

    def summarize_test(self, test, suite, **kwargs):
        suite["type"] = "pageload"
        if suite["subtests"] == {}:
            suite["subtests"] = []
        for measurement_name, replicates in test["measurements"].items():
            if not replicates:
                continue
            suite["subtests"].append(
                self._build_subtest(measurement_name, replicates, test)
            )
        suite["subtests"].sort(key=lambda subtest: subtest["name"])
