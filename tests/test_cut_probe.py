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
    mode, advanced = None, None
    for name, data in commands:
        if name == "mode":
            mode = data[-1]
        elif name == "advanced":
            advanced = data[-1]
        elif name == "raster":
            rows += 1
        elif name in ("ff", "eject"):
            if rows:
                pages.append({"rows": rows, "length_mm": round(rows * 25.4 / 180, 2),
                              "finish": name, "autocut": bool(mode & 64),
                              "chain_off_at_print": bool(advanced & 8)})
                rows = 0
            else:
                final.append(name)
    return {"pages": pages, "terminal_commands_after_pages": final,
            "init": sum(name == "init" for name, _ in commands),
            "eject": sum(name == "eject" for name, _ in commands)}


class CaptureTests(unittest.TestCase):
    def matrix(self, batches, extra=()):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.prn"
            subprocess.run([PROBE, "--capture", str(path), "--sequence", "batch",
                            "--batches", batches, "--precut", "1", "--tape-mm", "12",
                            *extra], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return decode(path.read_bytes())

    def capture(self, sequence, labels, mode, precut, tape=12, extra=()):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.prn"
            args = [PROBE, "--capture", str(path), "--sequence", sequence,
                    "--labels", str(labels), "--mode", mode, "--precut", str(precut),
                    "--tape-mm", str(tape), *extra]
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
                            expected = [64 if precut or (mode == "each" and i > 0) else 0
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

    def test_chain_off_after_raster_preserves_baseline_modes(self):
        for labels in (1, 3):
            for mode in ("each", "end"):
                for precut in (0, 1):
                    with self.subTest(labels=labels, mode=mode, precut=precut):
                        baseline = self.capture("baseline", labels, mode, precut, tape=24)
                        changed = self.capture("tail-chain-off", labels, mode, precut, tape=24)
                        self.assertEqual([data for name, data in changed if name == "raster"],
                                         [data for name, data in baseline if name == "raster"])
                        self.assertEqual([data for name, data in changed if name == "mode"],
                                         [data for name, data in baseline if name == "mode"])
                        self.assertEqual([data[-1] for name, data in changed if name == "advanced"],
                                         [0] * labels + [8])
                        self.assertEqual([name for name, data in changed[-3:]],
                                         ["raster", "advanced", "eject"])
                        self.assertEqual(summary(changed)["terminal_commands_after_pages"], [])

    def test_raster_payload_is_not_commands(self):
        payload = bytes([0x0C, 0x1A, 0x1B, 0x47]) * 4
        commands = decode(b"\x47\x10\x00" + payload + b"\x0c\x1a")
        self.assertEqual([name for name, _ in commands], ["raster", "ff", "eject"])

    def test_batch_four_modes_and_actual_label_markers(self):
        for labels in (1, 2, 3):
            for mode in ("chain", "each", "none", "end"):
                with self.subTest(labels=labels, mode=mode):
                    commands = self.capture("batch", labels, mode, 1)
                    result = summary(commands)
                    self.assertEqual(result["init"], 1)
                    self.assertEqual(result["terminal_commands_after_pages"], [])
                    self.assertEqual([p["rows"] for p in result["pages"]],
                                     [180, 216, 252][:labels])
                    self.assertEqual([p["autocut"] for p in result["pages"]],
                                     [True] * labels if mode in ("chain", "each") else
                                     [True] + [False] * (labels - 1))
                    finishes = ["ff"] * labels
                    if mode in ("each", "end"):
                        finishes[-1] = "eject"
                    self.assertEqual([p["finish"] for p in result["pages"]], finishes)
                    self.assertEqual([p["chain_off_at_print"] for p in result["pages"]],
                                     [False] * (labels - 1) + [mode in ("each", "end")])
                    # First and last row are solid edge bars. The first interior
                    # row differs per label, making accidental page replay visible.
                    rasters = [data[3:] for name, data in commands if name == "raster"]
                    position, interiors = 0, []
                    for page in result["pages"]:
                        pixels = rasters[position:position + page["rows"]]
                        self.assertEqual(pixels[0], pixels[-1])
                        self.assertNotEqual(pixels[0], pixels[2])
                        interiors.append(pixels[2])
                        position += page["rows"]
                    self.assertEqual(len(set(interiors)), labels)
                    if labels in (1, 3):
                        old = self.capture("baseline", labels, "each", 1)
                        self.assertEqual([data for name, data in commands if name == "raster"],
                                         [data for name, data in old if name == "raster"])
                    names = [name for name, _ in commands]
                    # Late finalization changes chain state only after final raster.
                    if mode in ("each", "end"):
                        self.assertEqual(names[-3:], ["raster", "advanced", "eject"])

    def test_chain_continuation_has_one_reset_and_one_final_command(self):
        commands = self.matrix("chain:3,chain:2,each:1")
        result = summary(commands)
        self.assertEqual(result["init"], 1)
        self.assertEqual(result["eject"], 1)
        self.assertEqual(result["terminal_commands_after_pages"], [])
        self.assertEqual([p["rows"] for p in result["pages"]], [180, 216, 252, 180, 216, 180])
        self.assertEqual([p["autocut"] for p in result["pages"]], [True] * 6)
        self.assertEqual([p["finish"] for p in result["pages"]], ["ff"] * 5 + ["eject"])
        self.assertEqual(sum(name == "rasterstart" for name, _ in commands), 3)

    def test_uncut_strips_preparation_is_once_per_batch(self):
        result = summary(self.matrix("none:2,none:2,end:1"))
        self.assertEqual(result["init"], 1)
        self.assertEqual(result["terminal_commands_after_pages"], [])
        self.assertEqual([p["autocut"] for p in result["pages"]],
                         [True, False, True, False, True])
        self.assertEqual([p["finish"] for p in result["pages"]], ["ff"] * 4 + ["eject"])

    def test_eight_label_hardware_gate_sequences(self):
        for batches, autocuts in (("chain:2,each:2", [True] * 4),
                                   ("none:2,end:2", [True, False, True, False])):
            with self.subTest(batches=batches):
                commands = self.matrix(batches)
                result = summary(commands)
                self.assertEqual(result["init"], 1)
                self.assertEqual(result["eject"], 1)
                self.assertEqual(result["terminal_commands_after_pages"], [])
                self.assertEqual([p["rows"] for p in result["pages"]],
                                 [180, 216, 180, 216])
                self.assertEqual([p["autocut"] for p in result["pages"]], autocuts)
                self.assertEqual([p["finish"] for p in result["pages"]],
                                 ["ff", "ff", "ff", "eject"])
                self.assertEqual([p["chain_off_at_print"] for p in result["pages"]],
                                 [False, False, False, True])

    def test_continuation_omits_only_init(self):
        fresh = self.capture("batch", 3, "end", 1)
        continued = self.capture("batch", 3, "end", 1, extra=("--continue", "1"))
        self.assertEqual(continued, fresh[1:])
        self.assertEqual(self.matrix("none:2,none:2,end:1", extra=("--wait-ready", "1")),
                         self.matrix("none:2,none:2,end:1"))

    def test_readiness_wait_does_not_add_print_commands(self):
        baseline = self.capture("tail-chain-off", 3, "each", 1, tape=24)
        self.assertEqual(self.capture("tail-chain-off", 3, "each", 1, tape=24,
                                     extra=("--wait-ready", "1")), baseline)

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
                             "--precut", "1", "--tape-mm", "12", "--device", "usb://anything"],
                            ["--sequence", "batch", "--labels", "3", "--mode", "end",
                             "--precut", "0", "--tape-mm", "12"],
                            ["--sequence", "batch", "--batches", "chain:3,chain:3,end:3",
                             "--precut", "1", "--tape-mm", "12"],
                            ["--sequence", "batch", "--batches", "none:2,",
                             "--precut", "1", "--tape-mm", "12"],
                            ["--sequence", "batch", "--labels", "1", "--mode", "end",
                             "--precut", "1", "--tape-mm", "12", "--continue", "2"],
                            ["--sequence", "batch", "--labels", "1", "--mode", "end",
                             "--precut", "1", "--tape-mm", "12",
                             "--device", "ptouch://Brother/PT-2730"]):
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
