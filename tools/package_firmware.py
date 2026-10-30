#!/usr/bin/env python3
"""Wrap an application .bin in an identity-checked ESP Device Core OTA package."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import zlib

IDENTITY_MAGIC = b"EDCIDENT2\0\0\0"
HEADER_SIZE = 256
FIELD = re.compile(r"[A-Za-z0-9_.-]+\Z")


def field(raw):
    value = raw.split(b"\0", 1)[0].decode("ascii")
    if b"\0" not in raw or not FIELD.fullmatch(value):
        raise ValueError("Invalid embedded firmware identity")
    return value


def identity(image):
    identities = set()
    start = 0
    while (start := image.find(IDENTITY_MAGIC, start)) >= 0:
        raw = image[start:start + 124]
        start += len(IDENTITY_MAGIC)
        if len(raw) != 124:
            continue
        try:
            identities.add((field(raw[12:44]), field(raw[44:92]), field(raw[92:124])))
        except (ValueError, UnicodeError):
            continue
    if len(identities) != 1:
        raise ValueError("Expected exactly one embedded identity; use ESP_DEVICE_FIRMWARE and config.firmware")
    return identities.pop()


def verify_image_target(image, board):
    if not image or image[0] != 0xE9:
        raise ValueError("Use an application .bin, not ELF, merged flash or filesystem image")
    if board == "esp8266":
        # ESP8266 images have an 8-byte header; first segment is usually mapped RAM.
        if len(image) < 16 or not 1 <= image[1] <= 16:
            raise ValueError("Invalid ESP8266 image header")
    else:
        ids = {"esp32": 0, "esp32s2": 2, "esp32c3": 5, "esp32s3": 9, "esp32c6": 13}
        if board not in ids or len(image) < 24 or struct.unpack_from("<H", image, 12)[0] != ids[board]:
            raise ValueError("Binary chip ID does not match the embedded board identity")


def package(image):
    board, application, version = identity(image)
    verify_image_target(image, board)
    header = bytearray(HEADER_SIZE)
    header[:8] = b"EDCOTA2\0"
    for offset, value in [(8, board), (40, application), (88, version)]:
        raw = value.encode("ascii")
        header[offset:offset + len(raw)] = raw
    struct.pack_into("<I", header, 120, len(image))
    header[124:156] = hashlib.md5(image).hexdigest().encode("ascii")
    struct.pack_into("<I", header, 252, zlib.crc32(header[:252]))
    return bytes(header) + image


def inspect_package(data):
    if len(data) < HEADER_SIZE or data[:8] != b"EDCOTA2\0":
        raise ValueError("Invalid package format")
    if zlib.crc32(data[:252]) != struct.unpack_from("<I", data, 252)[0]:
        raise ValueError("Damaged package header")
    payload = data[HEADER_SIZE:]
    if len(payload) != struct.unpack_from("<I", data, 120)[0]:
        raise ValueError("Incomplete package")
    if hashlib.md5(payload).hexdigest().encode("ascii") != data[124:156]:
        raise ValueError("Payload checksum mismatch")
    declared = field(data[8:40]), field(data[40:88]), field(data[88:120])
    if identity(payload) != declared:
        raise ValueError("Package labels do not match embedded firmware")
    verify_image_target(payload, declared[0])
    return {"board": declared[0], "application": declared[1], "version": declared[2],
            "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument("--inspect", action="store_true")
    args = parser.parse_args()
    try:
        data = args.image.read_bytes()
        if args.inspect:
            print(inspect_package(data))
        else:
            output = args.output or args.image.with_suffix(".edc")
            if output.resolve() == args.image.resolve():
                raise ValueError("Output must differ from input")
            wrapped = package(data)
            info = inspect_package(wrapped)
            output.write_bytes(wrapped)
            print(f"{output}: {info}")
    except (OSError, ValueError, UnicodeError) as exc:
        parser.exit(1, f"Error: {exc}\n")


if __name__ == "__main__":
    main()
