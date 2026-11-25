#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run with: python3 tests/cpu_lock.py /path/to/dosbox-x (requires NASM)."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


FIELDS = """
ud sp_before sp_after stack_top ud_ip db db_first db_last memory op_ip end_ip
ud_cs test_cs ax cx flags memory_hi
expected_ud sp_delta expected_memory expected_ax expected_cx flags_mask expected_flags tf
""".split()


def check_report(report):
    lines = report.splitlines()
    assert lines[-1] == "DONE", "Guest did not complete the test program"
    rows = {}
    for line in lines[:-1]:
        name, words = line.split(": ", 1)
        values = [int(word, 16) for word in words.split()]
        assert len(values) == len(FIELDS), line
        row = dict(zip(FIELDS, values))
        rows[name] = row
        expected = {
            "ud": row["expected_ud"],
            "sp_after": (row["sp_before"] + row["sp_delta"]) & 0xffff,
            "memory": row["expected_memory"],
            "memory_hi": 0x1234,
            "ax": row["expected_ax"],
            "cx": row["expected_cx"],
        }
        if row["ud"]:
            expected.update(ud_ip=row["op_ip"], ud_cs=row["test_cs"])
        if row["sp_delta"] == 0xfffe:
            expected["stack_top"] = 0x1357
        for field, value in expected.items():
            assert row[field] == value, f"{name}: {field}={row[field]:04x}, expected {value:04x}"
        assert row["flags"] & row["flags_mask"] == row["expected_flags"], name
        if not row["tf"]:
            assert row["db"] == 0, name
        elif not row["expected_ud"]:
            assert row["db"] == 1, f"{name}: expected one trap after the whole instruction"
            assert row["db_first"] == row["db_last"] == row["end_ip"], name
        print(f"  PASS {name}")
    if "lock_push_tf" in rows:
        # All faults currently produce a post-fault trap; this change preserves that behavior.
        for field in ("ud", "db", "db_first", "db_last"):
            assert rows["lock_push_tf"][field] == rows["ud_tf"][field], field
    return len(rows)


def main():
    emulator = Path(sys.argv[1]).resolve()
    source = Path(__file__).with_suffix(".asm")
    root = Path(tempfile.mkdtemp(prefix="dosbox-cpu-lock-"))
    print(f"Artifacts: {root}", flush=True)
    environment = os.environ.copy()
    environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    total = 0
    for core, cpu, defines, expected_count in (
        ("normal", "386", [], 64),
        ("normal", "386_prefetch", [], 64),
        ("simple", "386", [], 64),
        ("full", "386", [], 64),
        ("normal", "8086", ["-DLEGACY=1"], 3),
        ("normal", "8086_prefetch", ["-DLEGACY=1"], 3),
        ("normal", "286", ["-DLEGACY=1"], 3),
        ("normal", "286_prefetch", ["-DLEGACY=1"], 3),
        ("normal", "486old", ["-DLEGACY=1", "-DMODERN=1"], 5),
        ("normal", "486old_prefetch", ["-DLEGACY=1", "-DMODERN=1"], 5),
        ("simple", "486old", ["-DLEGACY=1", "-DMODERN=1"], 5),
    ):
        work = root / f"{core}-{cpu}"
        work.mkdir()
        subprocess.run(["nasm", "-f", "bin", *defines, "-o", str(work / "LOCK.COM"), str(source)], check=True)
        prefetch_test = not defines
        if prefetch_test:
            subprocess.run(["nasm", "-f", "bin", "-o", str(work / "PREFETCH.COM"),
                            str(source.with_name("cpu_lock_prefetch.asm"))], check=True)
        config = work / "dosbox.conf"
        config.write_text(f"""[sdl]
output=surface
[dosbox]
machine=vgaonly
memsize=4
quit warning=false
gdbserver=false
qmpserver=false
[cpu]
core={core}
cputype={cpu}
cycles=fixed 30000
integration device=false
[mixer]
nosound=true
[midi]
mpu401=none
mididevice=none
[sblaster]
sbtype=none
[gus]
gus=false
[speaker]
pcspeaker=false
tandy=off
disney=false
[joystick]
joysticktype=none
[serial]
serial1=disabled
serial2=disabled
serial3=disabled
serial4=disabled
[parallel]
parallel1=disabled
parallel2=disabled
parallel3=disabled
[printer]
printer=false
[dos]
automount=false
automountall=false
startcmd=false
network redirector=false
dos clipboard device enable=false
dos clipboard api=false
[ipx]
ipx=false
[ne2000]
ne2000=false
[autoexec]
mount c "{work}"
c:
LOCK.COM > REPORT.TXT
{"PREFETCH.COM > PREFETCH.TXT" if prefetch_test else ""}
exit
""")
        with (work / "emulator.log").open("wb") as log:
            subprocess.run(
                [str(emulator), "-conf", str(config), "-defaultdir", str(work), "-fastlaunch", "-nogui"],
                cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT,
                check=True, timeout=30,
            )
        print(f"{core}/{cpu}:")
        count = check_report((work / "REPORT.TXT").read_text(encoding="ascii"))
        assert count == expected_count, f"{core}/{cpu}: {count} cases, expected {expected_count}"
        total += count
        if prefetch_test:
            expected = "O" if cpu.endswith("_prefetch") else "U"
            result = (work / "PREFETCH.TXT").read_text(encoding="ascii")
            assert result == expected, f"{core}/{cpu}: self-modified LOCK gave {result!r}, expected {expected!r}"
            print(f"  PASS self_modified_lock")
            total += 1
    print(f"{total} cases passed")


if __name__ == "__main__":
    main()
