# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozharness

external_tools_path = os.path.join(
    os.path.abspath(os.path.dirname(os.path.dirname(mozharness.__file__))),
    "external_tools",
)

VSPATH = "{}/vs".format(os.environ["MOZ_FETCHES_DIR"])
VCPATH = "{}/VC/Tools/MSVC/14.29.30133".format(VSPATH)
SDKPATH = "{}/Windows Kits/10".format(VSPATH)
SDKVERSION = "10.0.19041.0"
SDKINCPATH = "{}/Include/{}".format(SDKPATH, SDKVERSION)
SDKLIBPATH = "{}/Lib/{}".format(SDKPATH, SDKVERSION)
SDKBINPATH = "{}/Bin/{}".format(SDKPATH, SDKVERSION)

config = {
    "exes": {
        "gittool.py": [os.path.join(external_tools_path, "gittool.py")],
        "python3": "python3",
    },
    "dump_syms_binary": "{}/dump_syms/dump_syms".format(os.environ["MOZ_FETCHES_DIR"]),
    "arch": "x86",
    "operating_system": "msvc",
    "partial_env": {
        "CFLAGS": ("-m32 -target i686-pc-windows-msvc -winsysroot {}".format(VSPATH)),
        "LDFLAGS": ("-target i686-pc-windows-msvc -winsysroot {}".format(VSPATH)),
        "PATH": (
            "{MOZ_FETCHES_DIR}/clang/bin:"
            "{MOZ_FETCHES_DIR}/binutils/bin:%(PATH)s".format(
                MOZ_FETCHES_DIR=os.environ["MOZ_FETCHES_DIR"]
            )
        ),
        "INCLUDES": (
            '-I"{_VCPATH}/include" '
            '-I"{_VCPATH}/atlmfc/include" '
            '-I"{_SDKINCPATH}/ucrt" '
            '-I"{_SDKINCPATH}/shared" '
            '-I"{_SDKINCPATH}/um" '
            '-I"{_SDKINCPATH}/winrt" '
        ).format(_VCPATH=VCPATH, _SDKINCPATH=SDKINCPATH),
        "LIB": (
            "{_VCPATH}/lib/x86;"
            "{_VCPATH}/atlmfc/lib/x86;"
            "{_SDKLIBPATH}/ucrt/x86;"
            "{_SDKLIBPATH}/um/x86;"
        ).format(_VCPATH=VCPATH, _SDKLIBPATH=SDKLIBPATH),
    },
}
