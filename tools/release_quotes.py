#!/usr/bin/env python3
"""Make an apostrophe-safe copy of the open-mpc mpc-vst-plugins tools (the L'Cronx quote problem).

    python3 tools/release_quotes.py <path to mpc-vst-plugins/tools> <out dir>

The upstream release.py puts the vendor name inside '...' in install.sh / uninstall.sh, so L'Cronx ends the quoted string early.
This copies the whole tools folder to <out dir>/tools and changes the substitution loop so that a name inside '...' is written
closed, escaped and reopened: L'Cronx -> L'\\''Cronx. release.sh then runs that copy and `sh -n` checks both installers.
"""
import os, shutil, sys

src, out = sys.argv[1], sys.argv[2]
dst = os.path.join(out, "tools")
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)
p = os.path.join(dst, "release.py")
s = open(p).read()
old = "    for k, v in sub.items():\n        text = text.replace(k, v)"
new = ("    for k, v in sub.items():   # inside '...' a quote must be closed, escaped and reopened: L'Cronx -> L'\\''Cronx\n"
       "        if k != \"@EXTRAS@\":    # (EXTRAS holds its own quoted words)\n"
       "            text = text.replace(\"'%s'\" % k, \"'%s'\" % v.replace(\"'\", \"'\\\\''\"))\n"
       "        text = text.replace(k, v)")
if old not in s:
    sys.exit("release.py: substitution loop not found (upstream changed?): patch by hand")
open(p, "w").write(s.replace(old, new))
print("patched", p)
