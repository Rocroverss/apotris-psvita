#!/usr/bin/env python3
#
# UNVERIFIED / DRAFT: modeled on tools/switchMakeNRO.py but has not been run
# against a real VitaSDK toolchain. Wires together the standard
# vita-elf-create -> vita-make-fself -> vita-mksfoex -> vita-pack-vpk
# pipeline (the same tools vitasdk's own vita.cmake macros wrap under the
# hood for vita_create_self()/vita_create_vpk()).
#
# TODO before this is actually usable:
#  - Real sce_sys/livearea assets (icon0.png, bg0.png, startup.png,
#    template.xml) -- sprites/favicon32.bmp is not sized/formatted for this
#    and isn't wired in below yet.
#  - Confirm TITLE_ID doesn't collide with anything else you have installed.

import os
import subprocess
import sys

TITLE_ID = "APOT00001"
APP_NAME = "Apotris"


def run(args):
    print("+ " + " ".join(str(a) for a in args))
    result = subprocess.run(args)
    if result.returncode != 0:
        print(f"Command failed: {args[0]}")
        sys.exit(1)


def main():
    vita_elf_create = sys.argv[1]
    vita_make_fself = sys.argv[2]
    vita_mksfoex = sys.argv[3]
    vita_pack_vpk = sys.argv[4]
    input_binary = sys.argv[5]
    version = sys.argv[6]
    build_dir = sys.argv[7]

    velf = os.path.join(build_dir, "apotris.velf")
    eboot = os.path.join(build_dir, "eboot.bin")
    param_sfo = os.path.join(build_dir, "param.sfo")
    output_vpk = input_binary + ".vpk"

    run([vita_elf_create, input_binary, velf])
    run([vita_make_fself, "-s", velf, eboot])
    run([vita_mksfoex, "-s", f"TITLE_ID={TITLE_ID}", APP_NAME, param_sfo])
    run(
        [
            vita_pack_vpk,
            "-s",
            param_sfo,
            "-b",
            eboot,
            output_vpk,
        ]
    )


if __name__ == "__main__":
    main()
