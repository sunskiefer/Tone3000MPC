"""Parse layout.conf widget lines into dicts (id, type, geometry, key, label, view). Shared by overlay.py."""
import re, shlex

VIEWS = ["Overview", "Chain", "Browse", "In-Out", "EQ", "Tuner", "Presets", "Setup", "Pictures"]
KINDS = ("knob", "button", "toggle", "readout", "popup", "list", "enum_h", "stepper")


def parse(path):
    out = []
    for n, ln in enumerate(open(path).read().split("\n"), 1):
        ln = ln.strip()
        if not ln or ln.startswith("#") or ln.split(" ")[0] not in KINDS:
            continue
        parts = shlex.split(ln)
        d = {"type": parts[0], "line": n}
        for p in parts[1:]:
            if "=" in p:
                k, v = p.split("=", 1)
                d[k] = v
        w = d.get("when", "")
        d["view"] = int(w.split(":")[1]) if w.startswith("view:") and w.split(":")[1].isdigit() else (-1 if w.startswith("in_sec") else -2)
        t = d["type"]
        if t == "knob":
            r = int(d.get("r", 55)); d["x"], d["y"], d["w"], d["h"] = int(d["cx"]) - r, int(d["cy"]) - r, 2 * r, 2 * r
        elif t == "list":
            d["x"], d["y"], d["w"], d["h"] = int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"])
        else:
            w_ = int(d.get("w", 289 if t == "toggle" else 290)); h_ = int(d.get("h", 86 if t == "toggle" else 56))
            d["x"], d["y"], d["w"], d["h"] = int(d["cx"]) - w_ // 2, int(d["cy"]) - h_ // 2, w_, h_
        out.append(d)
    for i, d in enumerate(out, 1):
        d["id"] = i
    return out
