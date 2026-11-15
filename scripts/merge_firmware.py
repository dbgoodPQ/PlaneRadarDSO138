# PlatformIO post-build script: merge bootloader + partitions + app into one .bin
# Usage: pio run -t merge -e cyd
# The result is flashed at 0x0 (esptool or a browser flasher).

Import("env")

import os
from os.path import join

# Second-stage bootloader offset differs by chip: the original ESP32 and the
# S2 keep it at 0x1000, newer chips (C3, S3, C6, ...) at 0x0.
BOOTLOADER_OFFSETS = {"esp32": "0x1000", "esp32s2": "0x1000"}


def flash_freq_arg(f_flash):
    """Board f_flash ("40000000L") -> esptool argument ("40m")."""
    hz = int(str(f_flash).rstrip("L"))
    return f"{hz // 1000000}m"


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    esptool = join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    boot_app0 = join(framework_dir, "tools", "partitions", "boot_app0.bin")
    merged = join(build_dir, "firmware-merged.bin")
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")
    flash_size = board.get("upload.flash_size", "4MB")
    flash_freq = flash_freq_arg(board.get("build.f_flash", "40000000L"))
    bootloader_offset = BOOTLOADER_OFFSETS.get(mcu, "0x0")

    bootloader = join(build_dir, "bootloader.bin")
    partitions = join(build_dir, "partitions.bin")
    firmware = join(build_dir, f"{progname}.bin")

    for path, label in (
        (bootloader, "bootloader.bin"),
        (partitions, "partitions.bin"),
        (boot_app0, "boot_app0.bin"),
        (firmware, f"{progname}.bin"),
    ):
        if not os.path.isfile(path):
            raise FileNotFoundError(f"Missing {label}: {path}")

    cmd = [
        env.subst("$PYTHONEXE"),
        esptool,
        "--chip",
        mcu,
        "merge_bin",
        "-o",
        merged,
        "--flash_mode",
        "keep",
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
        bootloader_offset,
        bootloader,
        "0x8000",
        partitions,
        "0xe000",
        boot_app0,
        "0x10000",
        firmware,
    ]
    print(f"Merging flash image -> {merged} ({mcu}, bootloader @ {bootloader_offset})")
    env.Execute(" ".join(f'"{c}"' if " " in c else c for c in cmd))
    return None


env.AddCustomTarget(
    name="merge",
    dependencies="${BUILD_DIR}/${PROGNAME}.bin",
    actions=env.Action(merge_firmware, "Merging flash image for web flasher"),
    title="Merge firmware",
    description="Create firmware-merged.bin (bootloader + partitions + app)",
)
