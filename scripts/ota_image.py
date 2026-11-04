"""Deterministic gzip OTA image and ESP8266 sector-aligned staging budget."""
import gzip
import hashlib
from pathlib import Path

SECTOR = 4096
ONE_MB_FS_START = 0xEB000  # eagle.flash.1m64.ld; preserve existing LittleFS.


def rounded(size):
    return (size + SECTOR - 1) // SECTOR * SECTOR


def ota_image(data, fs_start=ONE_MB_FS_START):
    if not data or data[0] != 0xE9:
        raise ValueError('OTA input must be an ESP8266 firmware image')
    compressed = gzip.compress(data, compresslevel=9, mtime=0)
    # Python 3.11/3.12 expose zlib's platform OS byte for mtime=0; normalize it.
    compressed = compressed[:9] + b'\xff' + compressed[10:]
    return compressed, verify_ota_image(data, compressed, fs_start)


def verify_ota_image(data, compressed, fs_start=ONE_MB_FS_START):
    if not data or data[0] != 0xE9 or gzip.decompress(compressed) != data:
        raise ValueError('OTA gzip does not match the serial firmware image')
    used = rounded(len(data)) + rounded(len(compressed))
    if used > fs_start:
        raise ValueError(f'OTA does not fit: sketch + gzip staging need {used} bytes, '
                         f'only {fs_start} available before LittleFS')
    return dict(compression='gzip', sketch_bytes=len(data),
                            upload_bytes=len(compressed), filesystem_start=fs_start,
                            staging_free_bytes=fs_start - rounded(len(data)),
                            margin_bytes=fs_start - used,
                            sketch_sha256=hashlib.sha256(data).hexdigest())


def write_if_changed(path, data):
    path = Path(path)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
