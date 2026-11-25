#!/usr/bin/env python3
"""
Self-check evaluator for the Lab 5 RV32IMF pipeline simulator.

usage:
    python3 evaluate.py <source_folder_or_zip> <test.s> <expected.txt> [--verbose]

What it does:
  1. Copies your source (a folder, or a .zip of it) into a temporary
     directory, replaces ui/ with the reference ui/ shipped next to this
     script, and runs `make`. Your own folder is never modified.
     If the build fails: prints "Cannot build from source" and exits.
  2. Runs  ./rvsim <test.s> --batch <options from the test's "# RUN:" line>
  3. Compares the final register state and the pipeline counters against
     <expected.txt> and prints

        register state match= X% ,counter match = Y%

     Which registers are compared is set by the test's "# GRADE:" line:
        # GRADE: int       x1..x31 + pc                      (32 values, default)
        # GRADE: int+fp    x1..x31 + pc + f0..f31 + fcsr     (65 values)
     X% is the percentage of those values that match (partial credit:
     every register counts equally).

Python 3.8+, standard library only.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REF_UI = os.path.join(HERE, "ref_ui")

BUILD_TIMEOUT_S = 180
RUN_TIMEOUT_S = 10

# Counters compared (forwards, CPI and accuracy are deliberately excluded:
# CPI/accuracy are derived from the others, forwards is not graded).
COUNTERS = ["cycles", "retired", "stalls", "branches", "mispredicts",
            "jal", "jalr", "redirects", "squashed"]

# Build artefacts never copied from the submission (forces a clean build).
SKIP_EXT = (".o", ".d", ".a", ".so")
SKIP_NAMES = {"rvsim", "rvsim.exe", ".git", "__MACOSX"}

REG_RE = re.compile(r"\bx(\d+)\s*/\s*\w+\s*=\s*(0x[0-9a-fA-F]+)")
FREG_RE = re.compile(r"\bf(\d+)\s*/\s*\w+\s*=\s*(0x[0-9a-fA-F]+)")
FCSR_RE = re.compile(r"^\s*fcsr\s*=\s*(0x[0-9a-fA-F]+)", re.M)
PC_RE = re.compile(r"^\s*pc\s*=\s*(0x[0-9a-fA-F]+)", re.M)
STATUS_RE = re.compile(r"^\s*status\s*=\s*(\S+)(?:\s+cycles\s*=\s*(\d+))?", re.M)
COUNTER_RE = re.compile(r"^\s*([a-z_]+)\s*=\s*(\S+)\s*$", re.M)


# ---------------------------------------------------------------- parsing

def parse_dump(text):
    """Extract registers, pc, status and counters from --batch output.
    Integer registers are keyed 1..31, FP registers "f0".."f31", plus
    "pc" and "fcsr"."""
    regs = {}
    for num, val in REG_RE.findall(text):
        n = int(num)
        if 1 <= n <= 31 and n not in regs:
            regs[n] = int(val, 16)
    for num, val in FREG_RE.findall(text):
        n = int(num)
        if 0 <= n <= 31 and ("f%d" % n) not in regs:
            regs["f%d" % n] = int(val, 16)
    m = PC_RE.search(text)
    if m:
        regs["pc"] = int(m.group(1), 16)
    m = FCSR_RE.search(text)
    if m:
        regs["fcsr"] = int(m.group(1), 16)

    status, counters = None, {}
    m = STATUS_RE.search(text)
    if m:
        status = m.group(1)
        if m.group(2) is not None:
            counters["cycles"] = int(m.group(2))
    for name, val in COUNTER_RE.findall(text):
        if name in COUNTERS and name != "cycles":
            try:
                counters[name] = int(val)
            except ValueError:
                pass
    return regs, status, counters


def run_options(asm_path):
    """Options from the first '# RUN:' header line of the test program.

    Accepted forms:   # RUN: --hazard stall --predictor btfn
                      # RUN: mylabel --hazard stall      (label is ignored)
    """
    with open(asm_path, errors="replace") as f:
        for line in f:
            m = re.match(r"\s*#\s*RUN:(.*)", line)
            if m:
                toks = shlex.split(m.group(1))
                while toks and not toks[0].startswith("--"):
                    toks.pop(0)          # drop optional label
                toks = [t for t in toks if t not in ("--batch", "--trace")]
                return toks
    return []


INT_KEYS = list(range(1, 32)) + ["pc"]
FP_KEYS = ["f%d" % i for i in range(32)] + ["fcsr"]


def grade_keys(asm_path):
    """Registers graded for this test, from its '# GRADE:' header line
    ('int' when there is none)."""
    with open(asm_path, errors="replace") as f:
        for line in f:
            m = re.match(r"\s*#\s*GRADE:\s*(\S+)", line)
            if m:
                g = m.group(1).lower()
                if g == "int":
                    return INT_KEYS, g
                if g == "int+fp":
                    return INT_KEYS + FP_KEYS, g
                raise ValueError("unknown '# GRADE: %s' (use int or int+fp)" % m.group(1))
    return INT_KEYS, "int"


def reg_name(k):
    return "x%d" % k if isinstance(k, int) else k


# ---------------------------------------------------------------- building

def find_project_root(path):
    """Directory containing the Makefile (top level, or one folder down,
    which is what zipping a folder usually produces)."""
    if os.path.isfile(os.path.join(path, "Makefile")):
        return path
    subs = [os.path.join(path, d) for d in os.listdir(path)
            if os.path.isdir(os.path.join(path, d)) and d not in SKIP_NAMES]
    hits = [d for d in subs if os.path.isfile(os.path.join(d, "Makefile"))]
    return hits[0] if len(hits) == 1 else None


def copy_source(src, dst):
    def ignore(_dir, names):
        return [n for n in names if n in SKIP_NAMES or n.endswith(SKIP_EXT)]
    shutil.copytree(src, dst, ignore=ignore, symlinks=False)


def prepare_and_build(source, work):
    """Returns (path_to_rvsim, None) or (None, reason)."""
    unpack = os.path.join(work, "unpacked")
    if os.path.isfile(source) and source.lower().endswith(".zip"):
        try:
            with zipfile.ZipFile(source) as z:
                for member in z.namelist():   # refuse paths escaping the dir
                    target = os.path.realpath(os.path.join(unpack, member))
                    if not target.startswith(os.path.realpath(unpack) + os.sep) \
                            and target != os.path.realpath(unpack):
                        return None, "zip contains unsafe path: " + member
                z.extractall(unpack)
        except zipfile.BadZipFile:
            return None, "not a valid zip file"
        root = find_project_root(unpack)
    elif os.path.isdir(source):
        root = find_project_root(source)
    else:
        return None, "source path is neither a folder nor a .zip"
    if root is None:
        return None, "no Makefile found at the top of the source folder"

    build = os.path.join(work, "build")
    copy_source(root, build)

    # Reference UI replaces the submitted one so the output format is fixed.
    ui_dir = os.path.join(build, "ui")
    if os.path.isdir(ui_dir):
        shutil.rmtree(ui_dir)
    shutil.copytree(REF_UI, ui_dir)

    try:
        p = subprocess.run(["make"], cwd=build, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=BUILD_TIMEOUT_S)
    except FileNotFoundError:
        return None, "`make` is not installed"
    except subprocess.TimeoutExpired:
        return None, "make took longer than %d s" % BUILD_TIMEOUT_S
    log = p.stdout.decode(errors="replace")
    if p.returncode != 0:
        errs = [l for l in log.splitlines() if re.search(r"error|Error|undefined reference", l)]
        return None, "make failed:\n    " + "\n    ".join((errs or log.splitlines())[-10:])
    exe = os.path.join(build, "rvsim")
    if not os.path.isfile(exe) or not os.access(exe, os.X_OK):
        return None, "make succeeded but produced no ./rvsim"
    return exe, None


def tail(text, n):
    return "\n".join(text.rstrip().splitlines()[-n:])


# ---------------------------------------------------------------- reporting

def pct(matched, total):
    if total == 0:
        return "N/A"
    v = 100.0 * matched / total
    return "%d%%" % v if v == int(v) else "%.1f%%" % v


RAW = False   # set by the (undocumented) --raw flag used by the batch grader


def print_result(reg_ok, reg_n, cnt_ok, cnt_n):
    print("register state match= %s ,counter match = %s"
          % (pct(reg_ok, reg_n), pct(cnt_ok, cnt_n)))
    if RAW:   # exact counts, so the batch grader never works from rounded %
        print("RAW reg=%d/%d cnt=%d/%d" % (reg_ok, reg_n, cnt_ok, cnt_n))


def main():
    ap = argparse.ArgumentParser(description="Lab 5 simulator self-check evaluator")
    ap.add_argument("source", help="your source folder (or a .zip of it)")
    ap.add_argument("program", help="test program (.s)")
    ap.add_argument("expected", help="expected --batch output (.txt)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every mismatching register / counter")
    ap.add_argument("--raw", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    global RAW
    RAW = a.raw

    if not os.path.exists(a.source):
        print("error: source folder '%s' not found" % a.source)
        return 2
    for path, what in ((a.program, "test program"), (a.expected, "expected output")):
        if not os.path.isfile(path):
            print("error: %s '%s' not found" % (what, path))
            return 2
    if not os.path.isdir(REF_UI):
        print("error: reference ui/ folder missing next to evaluate.py (%s)" % REF_UI)
        return 2

    try:
        reg_keys, grade = grade_keys(a.program)
    except ValueError as e:
        print("error: %s: %s" % (a.program, e))
        return 2
    with open(a.expected, errors="replace") as f:
        exp_regs, exp_status, exp_cnt = parse_dump(f.read())
    missing = [reg_name(k) for k in reg_keys if k not in exp_regs]
    if missing:
        print("error: '%s' does not look like a Lab 5 rvsim --batch dump "
              "(no value for %s)" % (a.expected, ", ".join(missing[:4])))
        return 2
    nreg = len(reg_keys)

    work = tempfile.mkdtemp(prefix="rvsim_eval_")
    try:
        exe, err = prepare_and_build(os.path.abspath(a.source), work)
        if exe is None:
            print("Cannot build from source")
            print("  reason: " + err)
            return 1

        opts = run_options(a.program)
        prog = os.path.join(work, "prog.s")
        shutil.copy(a.program, prog)
        cmd = [exe, prog, "--batch"] + opts
        if a.verbose:
            print("running: rvsim %s --batch %s" % (os.path.basename(a.program), " ".join(opts)))
            print("graded registers: %s (%d values)" % (
                "x1..x31, pc" if grade == "int" else "x1..x31, pc, f0..f31, fcsr", nreg))
        try:
            p = subprocess.run(cmd, cwd=work, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=RUN_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            print_result(0, nreg, 0, len(exp_cnt))
            print("  reason: simulator did not finish within %d s" % RUN_TIMEOUT_S)
            return 1
        out = p.stdout.decode(errors="replace")
        if p.returncode < 0:
            print_result(0, nreg, 0, len(exp_cnt))
            print("  reason: simulator crashed (signal %d)" % -p.returncode)
            return 1
        if p.returncode != 0:
            print_result(0, nreg, 0, len(exp_cnt))
            print("  reason: simulator exited with code %d:" % p.returncode)
            print("  " + tail(out + p.stderr.decode(errors="replace"), 5).replace("\n", "\n  "))
            return 1

        got_regs, got_status, got_cnt = parse_dump(out)

        reg_bad = [k for k in reg_keys if got_regs.get(k) != exp_regs[k]]
        cnt_keys = [c for c in COUNTERS if c in exp_cnt]
        cnt_bad = [c for c in cnt_keys if got_cnt.get(c) != exp_cnt[c]]

        print_result(len(reg_keys) - len(reg_bad), len(reg_keys),
                     len(cnt_keys) - len(cnt_bad), len(cnt_keys))

        if got_status != exp_status:
            print("  note: final status is %s, expected %s" % (got_status, exp_status))
        if a.verbose:
            def h(v):
                return "missing" if v is None else "0x%08x" % v
            for k in reg_bad:
                print("  register %-4s expected %s  got %s" % (reg_name(k), h(exp_regs[k]), h(got_regs.get(k))))
            for c in cnt_bad:
                g = got_cnt.get(c)
                print("  counter  %-12s expected %d  got %s" % (c, exp_cnt[c], "missing" if g is None else g))
        elif reg_bad or cnt_bad:
            print("  (run with --verbose to list the mismatches)")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
