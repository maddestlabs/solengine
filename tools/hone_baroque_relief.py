#!/usr/bin/env python3
"""
tools/hone_baroque_relief.py
============================
Redirect wrapper: delegates to tools/reprocess_baroque_relief.py.
"""
import subprocess
import sys

if __name__ == "__main__":
    sys.exit(subprocess.call([sys.executable, "tools/reprocess_baroque_relief.py"]))
