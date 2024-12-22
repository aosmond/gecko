# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

import mozharness

external_tools_path = os.path.join(
    os.path.abspath(os.path.dirname(os.path.dirname(mozharness.__file__))),
    "external_tools",
)

config = {
    "tooltool_manifest_file": "win64-aarch64.manifest",
    "exes": {
        "gittool.py": [sys.executable, os.path.join(external_tools_path, "gittool.py")],
        "python3": "c:\\mozilla-build\\python\\python3.exe",
    },
    "dump_syms_binary": "{}/dump_syms/dump_syms.exe".format(
        os.environ["MOZ_FETCHES_DIR"]
    ),
    "arch": "aarch64",
    "partial_env": {
        "PATH": os.environ["PATH"],
        "INCLUDES": os.environ["INCLUDES"],
        "LIB": os.environ["LIB"],
        "CFLAGS": ("--target=aarch64-windows-msvc"),
    },
}
