#!/usr/bin/env python3
"""CI-only compiler/linker launcher: runs the real command, echoes its output and
appends it to the file named by the FWA_CI_LOG environment variable, so a later
workflow step can turn compiler errors into GitHub annotations."""
import os
import subprocess
import sys

cmd = sys.argv[1:]
proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = proc.stdout.decode("utf-8", "replace")
sys.stdout.write(out)
sys.stdout.flush()
log = os.environ.get("FWA_CI_LOG")
if log and (out.strip() or proc.returncode != 0):
    with open(log, "a", encoding="utf-8") as f:
        f.write(f"===== exit {proc.returncode}: {' '.join(cmd)[-300:]}\n")
        f.write(out)
        f.write("\n")
sys.exit(proc.returncode)
