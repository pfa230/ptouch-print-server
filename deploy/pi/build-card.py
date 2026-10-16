#!/usr/bin/env python3
"""Write the ptouch Bluetooth print server SD card on macOS.

Usage: sudo python3 build-card.py <diskN> <raspios.img> <ptouch-image.tar.gz> <wifi.env> <ssh-pubkey-file>

wifi.env holds WIFI_SSID=, WIFI_PSK=, WIFI_COUNTRY=, LOGIN_PASSWORD= and optionally
BT_ADDRESS=. Only the password's SHA-512 crypt hash goes onto the card.
The card gets the stock image with partition 2 (root) grown to 6 GiB and a data
partition 3 filling the rest; cloud-init provisions everything on first boot.
"""
import json
import pathlib
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT_SECTORS = 6 * 1024 * 1024 * 2  # 6 GiB in 512-byte sectors
ALIGN = 8192


def diskutil(*args):
    return subprocess.run(["diskutil", *args], check=True, capture_output=True).stdout


def card_sectors(disk):
    info = plistlib.loads(diskutil("info", "-plist", disk))
    if info.get("BusProtocol") != "Secure Digital" or not info.get("Removable", info.get("RemovableMedia")):
        sys.exit(f"{disk} is not a removable SD card: {info.get('MediaName')} ({info.get('BusProtocol')})")
    return info["TotalSize"] // 512


def read_env(path):
    env = {}
    for line in pathlib.Path(path).read_text().splitlines():
        if line.strip() and not line.lstrip().startswith("#"):
            key, _, value = line.partition("=")
            env[key.strip()] = value.strip().strip('"\'')
    for key in ("WIFI_SSID", "WIFI_PSK", "WIFI_COUNTRY", "LOGIN_PASSWORD"):
        if not env.get(key):
            sys.exit(f"{path}: {key} is missing")
    return env


def write_partition_table(device, total):
    with open(device, "r+b", buffering=0) as f:
        mbr = bytearray(f.read(512))
        p2_start = struct.unpack_from("<I", mbr, 0x1BE + 16 + 8)[0]
        p3_start = -(-(p2_start + ROOT_SECTORS) // ALIGN) * ALIGN
        struct.pack_into("<I", mbr, 0x1BE + 16 + 12, p3_start - p2_start)
        # Partition 3: Linux (0x83), CHS fields unused (LBA only).
        mbr[0x1BE + 32:0x1BE + 48] = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 0x83, b"\xfe\xff\xff",
                                                  p3_start, total - p3_start)
        f.seek(0)
        f.write(mbr)


def main():
    disk, image, server_image, wifi_env, pubkey = sys.argv[1:6]
    total = card_sectors(disk)
    env = read_env(wifi_env)
    key = pathlib.Path(pubkey).read_text().strip()
    # Homebrew OpenSSL: macOS's LibreSSL /usr/bin/openssl has no SHA-512 crypt (-6).
    password_hash = subprocess.run(["/opt/homebrew/bin/openssl", "passwd", "-6", "-stdin"], input=env["LOGIN_PASSWORD"],
                                   text=True, check=True, capture_output=True).stdout.strip()

    diskutil("unmountDisk", disk)
    raw = f"/dev/r{disk}"
    print(f"Writing {image} to {raw} ...", flush=True)
    subprocess.run(["dd", f"if={image}", f"of={raw}", "bs=4m"], check=True)
    diskutil("unmountDisk", disk)
    write_partition_table(raw, total)

    diskutil("unmountDisk", disk)
    boot = pathlib.Path(tempfile.mkdtemp())
    diskutil("mount", "-mountPoint", str(boot), f"{disk}s1")

    user_data = (HERE / "user-data").read_text()
    for name, value in {"SSH_KEY": key, "PASSWORD_HASH": password_hash, "WIFI_COUNTRY": env["WIFI_COUNTRY"],
                        "BT_ADDRESS": env.get("BT_ADDRESS", "")}.items():
        user_data = user_data.replace(f"@{name}@", value)
    (boot / "user-data").write_text(user_data)
    (boot / "network-config").write_text(
        "network:\n  version: 2\n  wifis:\n    wlan0:\n      dhcp4: true\n      optional: false\n"
        f"      regulatory-domain: {json.dumps(env['WIFI_COUNTRY'])}\n"
        f"      access-points:\n        {json.dumps(env['WIFI_SSID'])}:\n"
        f"          password: {json.dumps(env['WIFI_PSK'])}\n")
    (boot / "ssh").touch()
    cmdline = boot / "cmdline.txt"
    cmdline.write_text(cmdline.read_text().replace(" resize", ""))
    (boot / "ptouch").mkdir()
    shutil.copy(server_image, boot / "ptouch/ptouch-image.tar.gz")

    diskutil("unmount", str(boot))
    diskutil("eject", disk)
    print(f"Done: {disk} ejected.")


main()
