#!/usr/bin/env python3
"""The single source of THING4000's parameter list.

Writes params.json (for mpc-vst-plugins' gen_vst.py) and src/param_ids.h (P_<KEY> index constants, names, ranges,
option lists and the Q-Link tables the engine needs).

    python3 tools/gen_params.py

MPC stores automation and saved values by parameter INDEX: this list is append-only once a version has shipped.
Nothing has shipped yet (0.1.0 is the first release), so the order below is free until then.

The panel is ONE tab; every page is a mode panel shown while the `view` parameter is at it (the PS3100XXL pattern:
overview tiles, OVERVIEW / < > in the header). The 16 Q-Links are the generic parameters ql1..ql16: the engine maps
Q-Link n to the n-th control of the current view (VIEW_QLINKS), so a Q-Link turn moves the control on screen and a
touch on the screen moves the Q-Link's little display (display_rev).
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

VIEWS = [("overview", "Overview"), ("chain", "Chain"), ("browse", "Browse"), ("inout", "In / Out"),
         ("eq", "EQ"), ("tuner", "Tuner"), ("presets", "Presets"), ("setup", "Setup"), ("pics", "Pictures")]
N_BLOCKS = 8          # blocks in the chain
N_ROWS = 8            # rows of the browse and preset lists
OFF_ON = ["Off", "On"]
GEARS = ["All", "Amp + Cab", "Amp Head", "Cabinet", "Pedal", "Outboard", "Spaces", "Experimental"]
FORMATS = ["All", "NAM", "IR"]
SORTS = ["Trending", "Popular", "Newest", "Oldest"]
IN_MODES = ["Left", "Right", "Sum"]
EQ_SCOPES = ["Global", "Block Pre", "Block Post"]

P = []          # the parameter list, in VST index order


def add(**kw):
    kw.setdefault("default", 0.0)
    P.append(kw)


def knob(key, name, lo, hi, default, **kw):
    add(key=key, name=name, min=lo, max=hi, default=default, dynamic_display=True, **kw)


def opts(key, name, options, default=0, **kw):
    add(key=key, name=name, options=options, default=default, **kw)


def trigger(key, name, **kw):
    add(key=key, name=name, min=0.0, max=1.0, momentary=True, type="trigger", **kw)


def text(key, name, **kw):
    """A read-only text value (the engine's get_param returns the words)."""
    kw.setdefault("type", "readout")
    add(key=key, name=name, min=0.0, max=0.0, display="string", **kw)


def integer(key, name, lo, hi, default, **kw):
    add(key=key, name=name, min=float(lo), max=float(hi), default=float(default), display="int", **kw)


# ---- navigation --------------------------------------------------------------------------------------------------
opts("view", "View", [v[1] for v in VIEWS], 0, refresh_display=True)
for k, lbl in (("nav_zoom", "Overview"), ("nav_prev", "Prev"), ("nav_next", "Next")):
    trigger(k, lbl, refresh_display=True)
for v in VIEWS[1:-1]:          # the Pictures view is appended at the very end (params.lock: never insert)
    trigger("go_" + v[0], "Go " + v[1], refresh_display=True)
opts("in_sec", "In Section", ["Overview", "Section"], 0)

# ---- chain -------------------------------------------------------------------------------------------------------
integer("sel", "Block", 1, N_BLOCKS, 1, refresh_display=True)
for i in range(1, N_BLOCKS + 1):
    add(key="blk_%d" % i, name="Block %d" % i, min=0.0, max=1.0, display="string", type="slot")
opts("move", "Move", OFF_ON, 0)
opts("b_bypass", "Bypass", OFF_ON, 0)
knob("b_level", "Level", -24.0, 24.0, 0.0)
knob("b_mix", "Mix", 0.0, 100.0, 100.0)
trigger("b_clear", "Clear")
text("b_model", "Model", type="stepper")
trigger("b_model_prev", "Model Prev")
trigger("b_model_next", "Model Next")
text("chain_info", "Chain Info")

# ---- in / out ----------------------------------------------------------------------------------------------------
opts("in_mode", "Input", IN_MODES, 2)
knob("in_gain", "In Gain", -24.0, 24.0, 0.0)
knob("out_gain", "Out Gain", -24.0, 24.0, 0.0)
knob("mix", "Dry/Wet", 0.0, 100.0, 100.0)
opts("gate_on", "Gate", OFF_ON, 0)
knob("gate_thr", "Gate Thr", -90.0, -20.0, -60.0)
opts("calib", "Calibrate", OFF_ON, 0)
knob("calib_ref", "Cal Level", 0.0, 24.0, 12.0)
opts("norm", "Normalize", OFF_ON, 1)
knob("norm_tgt", "Norm Target", -30.0, -6.0, -18.0)
opts("quality", "Quality", ["Full", "Lite"], 0)

# ---- eq (global, and the selected block's pre / post) ------------------------------------------------------------
opts("eq_scope", "EQ For", EQ_SCOPES, 0, refresh_display=True)
opts("eq_on", "EQ", OFF_ON, 0)
knob("eq_low", "Low", -12.0, 12.0, 0.0)
knob("eq_mid", "Mid", -12.0, 12.0, 0.0)
knob("eq_midf", "Mid Freq", 0.0, 1.0, 0.5)
knob("eq_high", "High", -12.0, 12.0, 0.0)
knob("g_hpf", "Low Cut", 0.0, 1.0, 0.0)
knob("g_lpf", "High Cut", 0.0, 1.0, 1.0)

# ---- tuner -------------------------------------------------------------------------------------------------------
opts("tuner_on", "Tuner", OFF_ON, 0)
opts("tuner_mute", "Mute", OFF_ON, 0)
integer("tuner_ref", "A =", 430, 450, 440)
text("tuner_note", "Note")
text("tuner_cents", "Cents")
text("tuner_hz", "Hz")

# ---- browse ------------------------------------------------------------------------------------------------------
opts("src", "Source", ["Local", "Online"], 0, refresh_display=True)
opts("gear", "Gear", GEARS, 0, refresh_display=True)
opts("fmt", "Format", FORMATS, 0, refresh_display=True)
opts("sort", "Sort", SORTS, 0, refresh_display=True)
integer("scroll", "Scroll", 0, 4000, 0)
for i in range(1, N_ROWS + 1):
    add(key="res_%d" % i, name="Row %d" % i, min=0.0, max=1.0, display="string", type="slot")
trigger("pg_prev", "Page Prev")
trigger("pg_next", "Page Next")
text("pg_info", "Page")
opts("del_mode", "Delete", OFF_ON, 0)
trigger("refresh", "Refresh")
text("status", "Status")
text("tgt", "Target")

# ---- presets -----------------------------------------------------------------------------------------------------
opts("pre_src", "Bank", ["User", "Factory"], 0, refresh_display=True)
integer("pre_scroll", "Scroll", 0, 4000, 0)
for i in range(1, N_ROWS + 1):
    add(key="pre_%d" % i, name="Preset %d" % i, min=0.0, max=1.0, display="string", type="slot")
trigger("pre_save", "Save New")
trigger("pre_over", "Overwrite")
opts("pre_del", "Delete", OFF_ON, 0)
opts("pre_autofill", "Auto Fill", OFF_ON, 1)
text("pre_info", "Preset")

# ---- setup -------------------------------------------------------------------------------------------------------
text("auth_info", "Account")
text("auth_code", "Code")
trigger("signin", "Sign In", refresh_display=True)
trigger("signout", "Sign Out", refresh_display=True)
text("dsp_load", "DSP Load")
text("folder", "Folder")
text("key_info", "API Key")
text("version", "Version")

# ---- overview tiles ----------------------------------------------------------------------------------------------
for v in VIEWS[1:-1]:
    text("tile_" + v[0], v[1] + " Tile")
text("ov_status", "Status")

# ---- the 16 Q-Links ----------------------------------------------------------------------------------------------
for i in range(16):
    add(key="ql%d" % (i + 1), name="Q-Link %d" % (i + 1), min=0.0, max=1.0, default=0.0,
        dynamic_name=True, dynamic_display=True)

# ---- the Q-Links of each view, in reading order ------------------------------------------------------------------
# Up to 16 keys per view, shown (top row left to right, then the next row) on the Force's knobs. Nothing is
# destructive here and no momentary button sits on a Q-Link.
VIEW_QL = {
    "overview": [],
    "chain": ["sel", "b_level", "b_mix", "b_bypass", "move"],
    "browse": ["scroll", "src", "gear", "fmt", "sort", "del_mode"],
    "inout": ["in_gain", "out_gain", "mix", "gate_thr", "calib_ref", "norm_tgt", "in_mode", "gate_on", "calib",
              "norm", "quality"],
    "eq": ["eq_low", "eq_mid", "eq_midf", "eq_high", "g_hpf", "g_lpf", "eq_scope", "eq_on"],
    "tuner": ["tuner_ref", "tuner_on", "tuner_mute"],
    "presets": ["pre_scroll", "pre_src", "pre_autofill", "pre_del"],
    "setup": [],
    "pics": ["sel", "move"],
}

# ---- captions: constant words the panel shows as live text, so they are drawn large (MPC's own font) ---------------------
CAPS = {"overview": "OVERVIEW", "prev": "<", "next": ">", "clear": "CLEAR", "mprev": "<", "mnext": ">", "pgprev": "< PAGE",
        "pgnext": "PAGE >", "refresh": "REFRESH", "save": "SAVE NEW", "over": "OVERWRITE", "signin": "SIGN IN",
        "signout": "SIGN OUT", "src": "SOURCE", "gear": "GEAR", "fmt": "FORMAT", "sort": "SORT", "input": "INPUT",
        "quality": "QUALITY", "eqfor": "EQ FOR", "bank": "BANK"}
for _k, _t in CAPS.items():
    text("cap_" + _k, "Caption " + _t)

# the Pictures page (added after 0.1.1: appended last so no saved project, automation or skin binding moves)
trigger("go_pics", "Go Pictures", refresh_display=True)
text("tile_pics", "Pictures Tile")

KEYS = [p["key"] for p in P]
assert len(KEYS) == len(set(KEYS)), "duplicate key"
INDEX = {k: i for i, k in enumerate(KEYS)}
for v, lst in VIEW_QL.items():
    assert len(lst) <= 16, v
    for k in lst:
        assert k in INDEX, (v, k)


def cname(key):
    return "P_" + key.upper()


def nopts(p):
    return len(p["options"]) if "options" in p else 0


def write_params_json():
    out = {"name": "THING4000", "params": P}
    with open(os.path.join(ROOT, "params.json"), "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")


def write_header():
    L = ["// generated by tools/gen_params.py: do not edit", "#pragma once", ""]
    L.append("enum ParamId {")
    for i, p in enumerate(P):
        L.append("  %s = %d," % (cname(p["key"]), i))
    L.append("  P_COUNT = %d" % len(P))
    L.append("};")
    L.append("")
    L.append("#define N_VIEWS %d" % len(VIEWS))
    for i, v in enumerate(VIEWS):
        L.append("#define V_%s %d" % (v[0].upper(), i))
    L.append("#define N_BLOCKS %d" % N_BLOCKS)
    L.append("#define N_ROWS %d" % N_ROWS)
    L.append("#define CAP_FIRST %s" % cname("cap_overview"))
    L.append("#define CAP_LAST %s" % cname("cap_bank"))
    L.append("")

    def arr(name, typ, vals):
        L.append("static const %s %s[P_COUNT] = {%s};" % (typ, name, ", ".join(vals)))

    arr("P_KEY", "char *const", ['"%s"' % p["key"] for p in P])
    arr("P_NAME", "char *const", ['"%s"' % p["name"].replace('"', "'") for p in P])
    arr("P_MIN", "float", [repr(float(p.get("min", 0.0))) + "f" for p in P])
    arr("P_MAX", "float", [repr(float(p["max"] if "max" in p else (len(p["options"]) - 1 if "options" in p else 1.0))) + "f" for p in P])
    arr("P_DEF", "float", [repr(float(p.get("default", 0.0))) + "f" for p in P])
    arr("P_NOPTS", "short", [str(nopts(p)) for p in P])
    caps = ['"%s"' % CAPS[p["key"][4:]] if p["key"].startswith("cap_") else "0" for p in P]
    arr("CAP_TEXT", "char *const", caps)
    arr("P_INT", "char", ["1" if (p.get("display") == "int" or "options" in p) else "0" for p in P])
    L.append("")
    for name, vals in (("GEAR_NAMES", GEARS), ("FMT_NAMES", FORMATS), ("SORT_NAMES", SORTS),
                       ("INMODE_NAMES", IN_MODES), ("SCOPE_NAMES", EQ_SCOPES), ("VIEW_NAMES", [v[1] for v in VIEWS])):
        L.append("static const char *const %s[] = {%s};" % (name, ", ".join('"%s"' % v for v in vals)))
    L.append("")
    L.append("// the views' controls in Q-Link order: up to 16 per view, -1 = none")
    L.append("static const short VIEW_QLINKS[N_VIEWS][16] = {")
    for v in VIEWS:
        ids = [cname(k) for k in VIEW_QL[v[0]]] + ["-1"] * (16 - len(VIEW_QL[v[0]]))
        L.append("  {%s}," % ", ".join(ids))
    L.append("};")
    with open(os.path.join(ROOT, "src", "param_ids.h"), "w") as f:
        f.write("\n".join(L) + "\n")


if __name__ == "__main__":
    write_params_json()
    write_header()
    print("%d parameters" % len(P))
