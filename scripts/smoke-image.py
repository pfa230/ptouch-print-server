#!/usr/bin/env python3
"""Exercise the deploy image through IPP and a socket sink, without USB access."""
import argparse
import html
import json
from pathlib import Path
import queue
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from test_cut_probe import summary


def unpackbits(payload):
    """Decode one TIFF PackBits row, bounded by the raster packet length."""
    decoded = bytearray()
    offset = 0
    while offset < len(payload):
        control = payload[offset]
        offset += 1
        if control < 128:
            count = control + 1
            if count > len(payload) - offset:
                raise ValueError("truncated PackBits literal")
            decoded.extend(payload[offset:offset + count])
            offset += count
        elif control > 128:
            if offset == len(payload):
                raise ValueError("truncated PackBits repeat")
            decoded.extend(payload[offset:offset + 1] * (257 - control))
            offset += 1
        if len(decoded) > 16:
            raise ValueError("oversized PackBits row")
    if len(decoded) != 16:
        raise ValueError("raster row does not contain all 128 head dots")
    return bytes(decoded)


def decode(data, model):
    """Check model setup and normalize raster packets for shared finishing checks."""
    commands = []
    offset = 0
    compressed = False
    fresh = False
    page_rows = expected_rows = 0
    p710bt = model == "PT-P710BT"
    escapes = {ord("R"): (4, "rasterstart"), ord("a"): (4, "rasterstart"),
               ord("c"): (8, "printinfo"), ord("z"): (13, "printinfo"),
               ord("M"): (4, "mode"), ord("K"): (4, "advanced"),
               ord("!"): (4, "notification"), ord("d"): (5, "margin")}
    while offset < len(data):
        op = data[offset]
        if op == 0:
            assert p710bt, "PT-2730 received invalidation"
            size, name = 1, "invalidate"
        elif op == 0x47:
            if len(data) - offset < 3 or data[offset + 2] != 0:
                raise ValueError("invalid raster header")
            size, name = 3 + data[offset + 1], "raster"
        elif data[offset:offset + 2] == b"\x1b@":
            size, name = 2, "init"
            fresh, compressed = True, False
        elif op == ord("M"):
            size, name = 2, "compression"
        elif data[offset:offset + 2] == b"\x1bi":
            if len(data) - offset < 3 or data[offset + 2] not in escapes:
                raise ValueError("invalid escape command")
            size, name = escapes[data[offset + 2]]
        elif op in (12, 26):
            size, name = 1, "ff" if op == 12 else "eject"
        else:
            raise ValueError(f"unknown command at {offset}")
        if size > len(data) - offset:
            raise ValueError(f"truncated {name} at {offset}")
        command = data[offset:offset + size]
        if name == "compression":
            assert p710bt and command == b"M\x02", command
            compressed = True
        elif name == "rasterstart":
            assert command == (b"\x1bia\x01" if p710bt else b"\x1biR\x01"), command
        elif name == "notification":
            assert p710bt and command == b"\x1bi!\x00", command
        elif name == "margin" and p710bt:
            assert command == b"\x1bid\x0e\x00", command
        elif name == "printinfo":
            assert command[:4] == (b"\x1biz\x84" if p710bt else b"\x1bic\x84"), command
            if p710bt:
                assert command[4] == 0 and command[5] == 12 and command[6] == 0 and command[12] == 0, command
                expected_rows = int.from_bytes(command[7:11], "little")
                assert command[11] == (0 if fresh else 1), command
                fresh = False
        elif name == "raster":
            assert compressed == p710bt
            head = unpackbits(command[3:]) if compressed else command[3:]
            assert len(head) == 16
            command = b"G\x10\x00" + head
            page_rows += 1
        elif name in ("ff", "eject"):
            if p710bt:
                assert page_rows == expected_rows, (page_rows, expected_rows)
            page_rows = 0
        commands.append((name, command))
        offset += size
    assert page_rows == 0, "capture contains an unterminated page"
    return commands


def page_rows(model):
    """Transmitted print-area rows for a 50 mm logical page on the model."""
    return 354 - (2 * 14 if model == "PT-P710BT" else 0)


def check_geometry(commands, copies, model):
    """Reconstruct the physical page: feed ends plus transmitted print area."""
    end = 14 if model == "PT-P710BT" else 0
    height = page_rows(model)
    rows = [data[3:] for name, data in commands if name == "raster"]
    assert len(rows) == height * copies
    for page in range(copies):
        for y in range(height):
            for x in range(128):
                actual = bool(rows[page * height + y][15 - x // 8] & (1 << (x % 8)))
                yy = y + end  # logical row, with the leading feed margin restored
                expected = 26 <= x < 102 and 87 <= yy < 267 and (x in (26, 101) or yy in (87, 266))
                assert actual == expected, (page, x, y, actual, expected)


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).stdout


def png(path, width=76):
    def chunk(name, payload):
        return (struct.pack("!I", len(payload)) + name + payload +
                struct.pack("!I", zlib.crc32(name + payload)))
    # A 76-dot imageable design on 12 mm tape, with explicit 180 dpi metadata.
    pixels = b"".join(b"\0" + bytes(0 if x in (0, width - 1) or y in (0, 179) else 255
                                   for x in range(width)) for y in range(180))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack("!2I5B", width, 180, 8, 0, 0, 0, 0)) +
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


def submit(url, directory, file, finishing="", copies=1, state=9, media="om_12x50mm_12x50mm"):
    request = operation("Print-Job", f'''ATTR mimeMediaType document-format image/png
  GROUP job-attributes-tag
  ATTR integer copies {copies}
  ATTR keyword media {media}
  ATTR keyword print-scaling none
  {finishing}
  FILE "{file}"''', "EXPECT job-id WITH-VALUE >0 DEFINE-VALUE job-id")
    request += operation("Get-Job-Attributes", "ATTR integer job-id $job-id",
                         f'''EXPECT job-state WITH-VALUE >5 REPEAT-NO-MATCH REPEAT-LIMIT 50
  EXPECT job-state WITH-VALUE {state}''')
    return ipp(url, directory, request)


def start_container(name, image, model, sink, state):
    run("docker", "run", "-d", "--name", name,
        "--mount", f"type=bind,src={state},dst=/var/lib/ptouch",
        "--add-host", "host.docker.internal:host-gateway",
        "-p", "127.0.0.1::8000", "-e",
        f"PTOUCH_DEVICE_URI=socket://host.docker.internal:{sink.port}",
        "-e", f"PTOUCH_MODEL={model}", image)
    mapping = json.loads(run("docker", "inspect", "--format",
                             "{{json .NetworkSettings.Ports}}", name))
    return mapping['8000/tcp'][0]['HostPort']


def ready(port, directory, model):
    url = f"ipp://127.0.0.1:{port}/ipp/print/ptouch"
    request = operation("Get-Printer-Attributes", "",
                        f'EXPECT printer-make-and-model WITH-VALUE "Brother {model}"')
    for attempt in range(20):
        try:
            ipp(url, directory, request)
            return url
        except RuntimeError:
            if attempt == 19:
                raise
            time.sleep(1)


def history_snapshot(url, directory, port):
    # Absolute dates survive a new printer uptime; relative time-at-* does not.
    fields = ("job-id", "job-name", "job-originating-user-name",
              "date-time-at-creation", "date-time-at-processing", "date-time-at-completed",
              "job-impressions", "job-impressions-completed",
              "job-state", "job-state-reasons", "job-state-message")
    jobs = ipp(url, directory, operation("Get-Jobs", "ATTR keyword which-jobs all", ""))
    ids = re.findall(r"job-id \(integer\) = (\d+)", jobs)
    assert ids, jobs
    # Get-Printers must expose exactly the configured queue after loading.
    printers = ipp(url, directory, operation("Get-Printers", "", "").replace("printer-uri $uri", f"system-uri ipp://127.0.0.1:{port}/ipp/system"))
    assert re.findall(r"printer-name \(nameWithoutLanguage\) = (.*)", printers) == ["ptouch"], printers
    pages = [urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=10).read().decode()
             for path in ("/", "/ptouch/jobs")]
    result = {}
    for job_id in ids:
        output = ipp(url, directory, operation("Get-Job-Attributes",
                     f"ATTR integer job-id {job_id}\n  ATTR keyword requested-attributes {','.join(fields)}", ""))
        attrs = {key: (kind, value) for key, kind, value in
                 re.findall(r"^\s+([\w-]+) \(([^)]+)\)(?: = (.*))?$", output, re.M)
                 if key in fields}
        assert set(attrs) == set(fields), output
        rows = []
        for page in pages:
            row = re.search(rf"<tr><td>{job_id}</td>.*?</tr>", page, re.S)
            assert row, (job_id, page)
            rows.append(html.unescape(row.group(0)))
        if attrs["job-state"][1] == "aborted":
            assert all(attrs["job-state-message"][1] in row for row in rows), (attrs, rows)
        result[job_id] = (attrs, rows)
    return result


def saved_state(name, directory):
    # docker cp also works after the identity-conflict container has exited.
    target = directory / "saved.state"
    run("docker", "cp", f"{name}:/var/lib/ptouch/ptouch-app.state", str(target))
    return target.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="already-built deploy image")
    parser.add_argument("--model", choices=("PT-2730", "PT-P710BT"), default="PT-2730",
                        help="selected printer model (default: PT-2730)")
    args = parser.parse_args()
    name = "ptouch-smoke-" + uuid.uuid4().hex[:10]
    sink = Sink()
    try:
        with tempfile.TemporaryDirectory(prefix="ptouch-smoke-") as temp:
            directory = Path(temp)
            state = directory / "state"
            state.mkdir()
            port = start_container(name, args.image, args.model, sink, state)
            url = ready(port, directory, args.model)
            image = directory / "label.png"
            png(image)
            attributes = operation("Get-Printer-Attributes",
                                   "ATTR keyword requested-attributes all", '''
  EXPECT printer-make-and-model WITH-VALUE "Brother MODEL"
  EXPECT finishings-supported COUNT 4 WITH-ALL-VALUES 3,11,60,63
  EXPECT finishings-default WITH-VALUE 11
  EXPECT finishings-col-database OF-TYPE collection
  EXPECT finishing-template-supported WITH-VALUE ptouch-chain
  EXPECT document-format-supported WITH-VALUE image/png WITH-DISTINCT-VALUES
  EXPECT document-format-supported WITH-ALL-VALUES "/^(application/octet-stream|image/(pwg-raster|urf|png|jpeg))$/"'''.replace("MODEL", args.model))
            print(ipp(url, directory, attributes))
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
                commands = decode(sink.take(), args.model)
                check_geometry(commands, 3, args.model)
                result = summary(commands)
                pages = result["pages"]
                assert len(pages) == 3, result
                assert [p["rows"] for p in pages] == [page_rows(args.model)] * 3, result
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
                commands = decode(sink.take(), args.model)
                check_geometry(commands, 2, args.model)
                result = summary(commands)
                assert [p["rows"] for p in result["pages"]] == [page_rows(args.model)] * 2, result
                assert [p["autocut"] for p in result["pages"]] == [True, False], result
                assert [p["chain_off_at_print"] for p in result["pages"]] == [False, at_end], result
                assert [p["finish"] for p in result["pages"]] == ["ff", "eject" if at_end else "ff"], result
                assert result["init"] == 1 and not result["terminal_commands_after_pages"], result
                print("default", value, json.dumps(result))
            oversized = directory / "oversized.png"
            png(oversized, width=77)
            print(submit(url, directory, oversized, state=8))
            assert sink.take() == b"", "Oversized image emitted printer commands"
            print(submit(url, directory, image, state=8, media="om_18x50mm_18x50mm"))
            assert sink.take() == b"", "Wrong-tape image emitted printer commands"
            malformed = directory / "malformed.png"
            malformed.write_bytes(b"\x89PNG\r\n\x1a\n")
            output = submit(url, directory, malformed, state=8)
            print(output)
            wire = sink.take()
            assert wire == b"", "Malformed PNG emitted printer commands"
            # A job canceled before it has data has no message and no reason bits.
            canceled = ipp(url, directory,
                           operation("Create-Job", "", "EXPECT job-id WITH-VALUE >0 DEFINE-VALUE job-id")
                           + operation("Cancel-Job", "ATTR integer job-id $job-id", "")
                           + operation("Get-Job-Attributes", "ATTR integer job-id $job-id",
                                       "EXPECT job-state WITH-VALUE 7"))
            print(canceled)
            canceled_id = re.search(r"job-id \(integer\) = (\d+)", canceled).group(1)
            # The landing page lists the failed jobs with their reasons.
            home = urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=10).read().decode()
            for result, reason in ((output, "PNG decode failed: "), (canceled, ": job-canceled-by-user")):
                job_id = re.search(r"job-id \(integer\) = (\d+)", result).group(1)
                row = re.search(rf"<tr><td>{job_id}</td>.*?</tr>", home, re.S)
                assert row and reason in row.group(0), row
            print(submit(url, directory, image, "ATTR enum finishings 60"))
            commands = decode(sink.take(), args.model)
            check_geometry(commands, 1, args.model)
            result = summary(commands)
            assert result["init"] == 0 and result["eject"] == 1, result
            assert len(result["pages"]) == 1 and not result["terminal_commands_after_pages"], result
            before = history_snapshot(url, directory, port)
            run("docker", "stop", "-t", "20", name)
            saved = saved_state(name, directory)
            assert b"message=" in saved and b"PNG decode failed:" in saved
            canceled_record = next(line for line in saved.decode().splitlines()
                                   if line.startswith("Job ") and f'id="{canceled_id}"' in line.split())
            assert 'state="7"' in canceled_record and "format=" not in canceled_record, canceled_record
            run("docker", "rm", name)
            port = start_container(name, args.image, args.model, sink, state)
            url = ready(port, directory, args.model)
            after = history_snapshot(url, directory, port)
            assert after[canceled_id][0]["job-state"][1] == "canceled", after[canceled_id]
            assert after == before, (before, after)

            run("docker", "stop", "-t", "20", name)
            saved = saved_state(name, directory)
            run("docker", "rm", name)
            replacement = "PT-P710BT" if args.model == "PT-2730" else "PT-2730"
            start_container(name, args.image, replacement, sink, state)
            for attempt in range(20):
                status = json.loads(run("docker", "inspect", "--format", "{{json .State}}", name))
                if not status["Running"]:
                    break
                time.sleep(1)
            assert not status["Running"] and status["ExitCode"] != 0, status
            logs = run("docker", "logs", name)
            assert "conflicts with configured printer" in logs and "fresh spool-directory" in logs, logs
            assert saved_state(name, directory) == saved, "Identity conflict changed saved history"
            print(args.model, "image smoke passed: presets, geometry, failures, persistent history, and preserved history on printer conflict.")
    finally:
        subprocess.run(["docker", "logs", name], check=False)
        subprocess.run(["docker", "rm", "-f", name], check=False,
                       stdout=subprocess.DEVNULL)
        sink.socket.close()


if __name__ == "__main__":
    main()
