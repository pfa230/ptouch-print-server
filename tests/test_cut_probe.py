#!/usr/bin/env python3
"""Check captures without treating opcode bytes inside raster data as commands."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


def decode(data):
    commands = []
    offset = 0
    while offset < len(data):
        start = offset
        op = data[offset]
        if op == 0x47:
            if len(data) - offset < 3 or data[offset + 2] != 0:
                raise ValueError(f"invalid raster header at {offset}")
            size = 3 + data[offset + 1]
            name = "raster"
        elif data[offset:offset + 2] == b"\x1b@":
            size, name = 2, "init"
        elif data[offset:offset + 3] == b"\x1biR":
            size, name = 4, "rasterstart"
        elif data[offset:offset + 3] == b"\x1bic":
            size, name = 8, "printinfo"
        elif data[offset:offset + 3] == b"\x1biM":
            size, name = 4, "mode"
        elif data[offset:offset + 3] == b"\x1biK":
            size, name = 4, "advanced"
        elif op in (0x0C, 0x1A):
            size, name = 1, "ff" if op == 0x0C else "eject"
        else:
            raise ValueError(f"unknown command at {offset}")
        if len(data) - offset < size:
            raise ValueError(f"truncated {name} at {offset}")
        offset += size
        commands.append((name, data[start:offset]))
    return commands


def summary(commands):
    pages, rows, final = [], 0, []
    for name, data in commands:
        if name == "raster":
            rows += 1
        elif name in ("ff", "eject"):
            if rows:
                pages.append({"rows": rows, "length_mm": round(rows * 25.4 / 180, 2),
                              "finish": name})
                rows = 0
            else:
                final.append(name)
    return {"pages": pages, "terminal_commands_after_pages": final,
            "init": sum(name == "init" for name, _ in commands),
            "eject": sum(name == "eject" for name, _ in commands)}


class CaptureTests(unittest.TestCase):
    def capture(self, sequence, labels, mode, precut, tape=12):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.prn"
            args = [PROBE, "--capture", str(path), "--sequence", sequence,
                    "--labels", str(labels), "--mode", mode, "--precut", str(precut),
                    "--tape-mm", str(tape)]
            subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return decode(path.read_bytes())

    def test_baseline_and_drain_matrix(self):
        for labels in (1, 3):
            for mode in ("each", "end"):
                for precut in (0, 1):
                    with self.subTest(labels=labels, mode=mode, precut=precut):
                        baseline = self.capture("baseline", labels, mode, precut)
                        drain = self.capture("drain", labels, mode, precut)
                        for sequence, commands in (("baseline", baseline), ("drain", drain)):
                            result = summary(commands)
                            self.assertEqual(result["init"], 1)
                            self.assertEqual(result["eject"], 1)
                            self.assertEqual([p["rows"] for p in result["pages"]],
                                             [180 + i * 36 for i in range(labels)])
                            self.assertEqual([p["finish"] for p in result["pages"]],
                                             ["ff"] * labels if sequence == "drain" else
                                             ["ff"] * (labels - 1) + ["eject"])
                            self.assertEqual(result["terminal_commands_after_pages"],
                                             ["eject"] if sequence == "drain" else [])
                            advanced = [data[-1] for name, data in commands if name == "advanced"]
                            self.assertEqual(advanced, [0] * labels if sequence == "drain" else
                                             [0] * (labels - 1) + [8])
                            modes = [data[-1] for name, data in commands if name == "mode"]
                            expected = [64 if precut or (mode == "each" and
                                        (sequence == "drain" or i < labels - 1)) else 0
                                        for i in range(labels)]
                            self.assertEqual(modes, expected)
                        # Changing finalization must not change the actual fixture pixels.
                        self.assertEqual([data for name, data in baseline if name == "raster"],
                                         [data for name, data in drain if name == "raster"])
                        self.assertEqual([name for name, _ in drain[-2:]], ["ff", "eject"])

    def test_tail_eject_only_changes_final_terminator(self):
        for labels in (1, 3):
            for mode in ("each", "end"):
                for precut in (0, 1):
                    with self.subTest(labels=labels, mode=mode, precut=precut):
                        drain = self.capture("drain", labels, mode, precut, tape=24)
                        tail = self.capture("tail-eject", labels, mode, precut, tape=24)
                        # Same continuation settings and pixels, but one final
                        # print-with-feed instead of FF plus an empty-page eject.
                        self.assertEqual(tail, drain[:-2] + [("eject", b"\x1a")])
                        result = summary(tail)
                        self.assertEqual(result["terminal_commands_after_pages"], [])
                        self.assertEqual([p["finish"] for p in result["pages"]],
                                         ["ff"] * (labels - 1) + ["eject"])

    def test_raster_payload_is_not_commands(self):
        payload = bytes([0x0C, 0x1A, 0x1B, 0x47]) * 4
        commands = decode(b"\x47\x10\x00" + payload + b"\x0c\x1a")
        self.assertEqual([name for name, _ in commands], ["raster", "ff", "eject"])

    def test_truncated_capture_rejected(self):
        for data in (b"\x47", b"\x47\x10\x00\x0c\x1a", b"\x1biK"):
            with self.assertRaises(ValueError):
                decode(data)

    def test_invalid_arguments_do_not_create_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.prn"
            for invalid in ([], ["--sequence", "drain", "--labels", "0", "--mode", "each",
                                  "--precut", "1", "--tape-mm", "12"],
                            ["--sequence", "drain", "--labels", "1", "--mode", "each",
                             "--precut", "1", "--tape-mm", "12", "--device", "usb://anything"]):
                result = subprocess.run([PROBE, "--capture", str(path)] + invalid,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(path.exists())


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--inspect":
        print(json.dumps(summary(decode(Path(sys.argv[2]).read_bytes())), indent=2))
    else:
        PROBE = sys.argv.pop(1)
        unittest.main()
