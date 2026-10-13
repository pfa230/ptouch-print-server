#!/usr/bin/env python3
"""Exercise the deploy image through IPP and a socket sink, without USB access."""
import argparse
import json
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from test_cut_probe import decode, summary


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).stdout


def png(path):
    def chunk(name, payload):
        return (struct.pack("!I", len(payload)) + name + payload +
                struct.pack("!I", zlib.crc32(name + payload)))
    # A 76-dot imageable design on 12 mm tape, with explicit 180 dpi metadata.
    pixels = b"".join(b"\0" + bytes(0 if x in (0, 75) or y in (0, 179) else 255
                                   for x in range(76)) for y in range(180))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack("!2I5B", 76, 180, 8, 0, 0, 0, 0)) +
                     chunk(b"pHYs", struct.pack("!2IB", 7087, 7087, 1)) +
                     chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


class Sink:
    def __init__(self, port=0):
        self.socket = socket.socket()
        self.socket.bind(("0.0.0.0", port))
        self.socket.listen()
        self.port = self.socket.getsockname()[1]
        self.jobs = queue.Queue()
        self.thread = threading.Thread(target=self.capture, daemon=True)
        self.thread.start()

    def capture(self):
        while True:
            try:
                connection, _ = self.socket.accept()
            except OSError:
                return
            with connection:
                data = bytearray()
                while block := connection.recv(65536):
                    data.extend(block)
                self.jobs.put(bytes(data))

    def take(self, timeout=10):
        return self.jobs.get(timeout=timeout)


def operation(name, body, expect):
    return f'''{{
  OPERATION {name}
  GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8
  ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri
  ATTR name requesting-user-name smoke
  {body}
  STATUS successful-ok
  {expect}
}}
'''


def ipp(url, directory, text):
    path = directory / "request.test"
    path.write_text(text)
    try:
        return run("ipptool", "-tv", "-T", "10", url, str(path))
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stdout) from error


def submit(url, directory, file, finishing="", copies=1, state=9):
    request = operation("Print-Job", f'''ATTR mimeMediaType document-format image/png
  GROUP job-attributes-tag
  ATTR integer copies {copies}
  ATTR keyword media om_12x50mm_12x50mm
  ATTR keyword print-scaling none
  {finishing}
  FILE "{file}"''', "EXPECT job-id WITH-VALUE >0 DEFINE-VALUE job-id")
    request += operation("Get-Job-Attributes", "ATTR integer job-id $job-id",
                         f'''EXPECT job-state WITH-VALUE >5 REPEAT-NO-MATCH REPEAT-LIMIT 50
  EXPECT job-state WITH-VALUE {state}''')
    return ipp(url, directory, request)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="already-built deploy image")
    args = parser.parse_args()
    name = "ptouch-smoke-" + uuid.uuid4().hex[:10]
    sink = Sink()
    try:
        run("docker", "run", "-d", "--name", name,
            "--add-host", "host.docker.internal:host-gateway",
            "-p", "127.0.0.1::8000", "-e",
            f"PTOUCH_DEVICE_URI=socket://host.docker.internal:{sink.port}", args.image)
        mapping = json.loads(run("docker", "inspect", "--format",
                                 "{{json .NetworkSettings.Ports}}", name))
        url = f"ipp://127.0.0.1:{mapping['8000/tcp'][0]['HostPort']}/ipp/print/ptouch"
        with tempfile.TemporaryDirectory(prefix="ptouch-smoke-") as temp:
            directory = Path(temp)
            image = directory / "label.png"
            png(image)
            attributes = operation("Get-Printer-Attributes",
                                   "ATTR keyword requested-attributes all", '''
  EXPECT finishings-supported COUNT 4 WITH-ALL-VALUES 3,11,60,63
  EXPECT finishings-default WITH-VALUE 11
  EXPECT finishings-col-database OF-TYPE collection
  EXPECT finishing-template-supported WITH-VALUE ptouch-chain
  EXPECT document-format-supported WITH-VALUE image/png WITH-DISTINCT-VALUES
  EXPECT document-format-supported WITH-ALL-VALUES "/^(application/octet-stream|image/(pwg-raster|urf|png|jpeg))$/"''')
            for attempt in range(20):
                try:
                    print(ipp(url, directory, attributes))
                    break
                except RuntimeError:
                    if attempt == 19:
                        raise
                    time.sleep(1)
            cases = (
                ("default", "", [True] * 3, True, 1),
                ("none", "ATTR enum finishings 3", [True, False, False], False, 1),
                ("chain", '''ATTR collection finishings-col {
    MEMBER keyword finishing-template ptouch-chain
  }''', [True] * 3, False, 0),
                ("each", "ATTR enum finishings 60", [True] * 3, True, 0),
                ("end", "ATTR enum finishings 63", [True, False, False], True, 1),
            )
            for label, finishing, autocuts, at_end, resets in cases:
                print(submit(url, directory, image, finishing, copies=3))
                result = summary(decode(sink.take()))
                pages = result["pages"]
                assert len(pages) == 3, result
                assert [p["rows"] for p in pages] == [354] * 3, result
                assert [p["autocut"] for p in pages] == autocuts, result
                assert [p["chain_off_at_print"] for p in pages] == [False, False, at_end], result
                assert [p["finish"] for p in pages] == ["ff", "ff", "eject" if at_end else "ff"], result
                assert result["init"] == resets, result
                assert result["eject"] == int(at_end), result
                assert not result["terminal_commands_after_pages"], result
                print(label, json.dumps(result))
            # Printer defaults use the same standard enum/collection boundary.
            for value, out_type, at_end in ((63, "collection", True), (3, "no-value", False)):
                # Admin IPP operations require local access. The existing CLI
                # issues Set-Printer-Attributes through the container's socket.
                print(run("docker", "exec", name, "ptouch-app", "modify", "-d", "ptouch",
                          "-o", f"finishings={value}"))
                print(ipp(url, directory, operation("Get-Printer-Attributes",
                    "ATTR keyword requested-attributes finishings-default,finishings-col-default",
                    f'''EXPECT finishings-default WITH-VALUE {value}
  EXPECT finishings-col-default OF-TYPE {out_type}''')))
                print(submit(url, directory, image, copies=2))
                result = summary(decode(sink.take()))
                assert [p["rows"] for p in result["pages"]] == [354, 354], result
                assert [p["autocut"] for p in result["pages"]] == [True, False], result
                assert [p["chain_off_at_print"] for p in result["pages"]] == [False, at_end], result
                assert [p["finish"] for p in result["pages"]] == ["ff", "eject" if at_end else "ff"], result
                assert result["init"] == 1 and not result["terminal_commands_after_pages"], result
                print("default", value, json.dumps(result))
            malformed = directory / "malformed.png"
            malformed.write_bytes(b"\x89PNG\r\n\x1a\n")
            print(submit(url, directory, malformed, state=8))
            wire = sink.take()
            assert wire == b"", "Malformed PNG emitted printer commands"
            print("Image smoke passed: four presets, synchronized defaults, geometry, copies, and input failure.")
    finally:
        subprocess.run(["docker", "logs", name], check=False)
        subprocess.run(["docker", "rm", "-f", name], check=False,
                       stdout=subprocess.DEVNULL)
        sink.socket.close()


if __name__ == "__main__":
    main()
