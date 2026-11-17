# Control map

Single source of layout: `layout.conf` (shadow pixels, plugin area x 0..1280, y 86..714). `x y w h` below is the control's
box; to move control N, edit its line in `layout.conf` (the `line` column) and nothing else changes. Numbered overlays: `docs/img/overlay_*.png`.

| id | type | page | x | y | w | h | label | param key | layout.conf line |
|---|---|---|---|---|---|---|---|---|---|
| 1 | button | header | 15 | 96 | 150 | 56 | cap_overview | nav_zoom | 36 |
| 2 | button | header | 195 | 96 | 90 | 56 | cap_prev | nav_prev | 37 |
| 3 | button | header | 305 | 96 | 90 | 56 | cap_next | nav_next | 38 |
| 4 | button | Overview | 25 | 181 | 290 | 110 | CHAIN | go_chain | 41 |
| 5 | button | Overview | 345 | 181 | 290 | 110 | BROWSE | go_browse | 42 |
| 6 | button | Overview | 665 | 181 | 290 | 110 | IN / OUT | go_inout | 43 |
| 7 | button | Overview | 985 | 181 | 290 | 110 | EQ | go_eq | 44 |
| 8 | button | Overview | 25 | 345 | 290 | 110 | TUNER | go_tuner | 45 |
| 9 | button | Overview | 345 | 345 | 290 | 110 | PRESETS | go_presets | 46 |
| 10 | button | Overview | 665 | 345 | 290 | 110 | SETUP | go_setup | 47 |
| 11 | readout | Overview | 25 | 288 | 290 | 36 |  | tile_chain | 48 |
| 12 | readout | Overview | 345 | 288 | 290 | 36 |  | tile_browse | 49 |
| 13 | readout | Overview | 665 | 288 | 290 | 36 |  | tile_inout | 50 |
| 14 | readout | Overview | 985 | 288 | 290 | 36 |  | tile_eq | 51 |
| 15 | readout | Overview | 25 | 452 | 290 | 36 |  | tile_tuner | 52 |
| 16 | readout | Overview | 345 | 452 | 290 | 36 |  | tile_presets | 53 |
| 17 | readout | Overview | 665 | 452 | 290 | 36 |  | tile_setup | 54 |
| 18 | button | Overview | 985 | 345 | 290 | 110 | PICTURES | go_pics | 55 |
| 19 | readout | Overview | 985 | 452 | 290 | 36 |  | tile_pics | 56 |
| 20 | readout | Overview | 985 | 500 | 290 | 50 |  | dsp_load | 57 |
| 21 | readout | Overview | 20 | 558 | 1240 | 44 |  | ov_status | 58 |
| 22 | list | Chain | 16 | 165 | 1248 | 128 |  | blk | 61 |
| 23 | readout | Chain | 20 | 302 | 1240 | 40 |  | chain_info | 62 |
| 24 | button | Chain | 30 | 362 | 100 | 60 | cap_mprev | b_model_prev | 63 |
| 25 | readout | Chain | 140 | 362 | 800 | 60 |  | b_model | 64 |
| 26 | button | Chain | 950 | 362 | 100 | 60 | cap_mnext | b_model_next | 65 |
| 27 | button | Chain | 1060 | 362 | 200 | 60 | cap_clear | b_clear | 66 |
| 28 | knob | Chain | 103 | 473 | 114 | 114 | LEVEL | b_level | 67 |
| 29 | knob | Chain | 424 | 474 | 112 | 112 | MIX | b_mix | 68 |
| 30 | toggle | Chain | 656 | 487 | 289 | 86 | BYPASS | b_bypass | 69 |
| 31 | toggle | Chain | 976 | 487 | 289 | 86 | MOVE | move | 70 |
| 32 | readout | Browse | 345 | 145 | 290 | 40 |  | cap_gear | 73 |
| 33 | popup | Browse | 345 | 177 | 290 | 56 |  | gear | 74 |
| 34 | readout | Browse | 665 | 145 | 290 | 40 |  | cap_fmt | 75 |
| 35 | popup | Browse | 665 | 177 | 290 | 56 |  | fmt | 76 |
| 36 | list | Browse | 16 | 250 | 1248 | 216 |  | res | 77 |
| 37 | readout | Browse | 40 | 480 | 520 | 40 |  | pg_info | 78 |
| 38 | readout | Browse | 580 | 480 | 440 | 40 |  | tgt | 79 |
| 39 | readout | Browse | 20 | 536 | 960 | 40 |  | status | 80 |
| 40 | button | Browse | 20 | 617 | 220 | 56 | cap_pgprev | pg_prev | 81 |
| 41 | button | Browse | 270 | 617 | 220 | 56 | cap_pgnext | pg_next | 82 |
| 42 | button | Browse | 520 | 617 | 220 | 56 | cap_refresh | refresh | 83 |
| 43 | toggle | Browse | 736 | 597 | 289 | 86 | DELETE | del_mode | 84 |
| 44 | knob | Browse | 1092 | 490 | 116 | 116 | SCROLL | scroll | 85 |
| 45 | knob | In-Out | 102 | 204 | 116 | 116 | IN GAIN | in_gain | 88 |
| 46 | knob | In-Out | 423 | 205 | 114 | 114 | OUT GAIN | out_gain | 89 |
| 47 | knob | In-Out | 744 | 206 | 112 | 112 | DRY/WET | mix | 90 |
| 48 | knob | In-Out | 1065 | 207 | 110 | 110 | GATE THR | gate_thr | 91 |
| 49 | knob | In-Out | 106 | 424 | 108 | 108 | CAL LEVEL | calib_ref | 92 |
| 50 | knob | In-Out | 427 | 425 | 106 | 106 | NORM TGT | norm_tgt | 93 |
| 51 | toggle | In-Out | 616 | 407 | 289 | 86 | GATE | gate_on | 94 |
| 52 | toggle | In-Out | 616 | 497 | 289 | 86 | CALIBRATE | calib | 95 |
| 53 | toggle | In-Out | 616 | 587 | 289 | 86 | NORMALIZE | norm | 96 |
| 54 | readout | In-Out | 945 | 432 | 290 | 40 |  | cap_input | 97 |
| 55 | popup | In-Out | 945 | 466 | 290 | 56 |  | in_mode | 98 |
| 56 | readout | In-Out | 945 | 528 | 290 | 40 |  | cap_quality | 99 |
| 57 | popup | In-Out | 945 | 562 | 290 | 56 |  | quality | 100 |
| 58 | readout | EQ | 300 | 145 | 360 | 40 |  | cap_eqfor | 103 |
| 59 | popup | EQ | 300 | 177 | 360 | 56 |  | eq_scope | 104 |
| 60 | toggle | EQ | 856 | 147 | 289 | 86 | EQ ON | eq_on | 105 |
| 61 | knob | EQ | 102 | 272 | 116 | 116 | LOW | eq_low | 106 |
| 62 | knob | EQ | 423 | 273 | 114 | 114 | MID | eq_mid | 107 |
| 63 | knob | EQ | 744 | 274 | 112 | 112 | MID FREQ | eq_midf | 108 |
| 64 | knob | EQ | 1065 | 275 | 110 | 110 | HIGH | eq_high | 109 |
| 65 | knob | EQ | 106 | 491 | 108 | 108 | LOW CUT | g_hpf | 110 |
| 66 | knob | EQ | 427 | 492 | 106 | 106 | HIGH CUT | g_lpf | 111 |
| 67 | readout | EQ | 550 | 525 | 700 | 40 |  | chain_info | 112 |
| 68 | readout | Tuner | 290 | 200 | 700 | 140 |  | tuner_note | 115 |
| 69 | readout | Tuner | 190 | 365 | 900 | 70 |  | tuner_cents | 116 |
| 70 | readout | Tuner | 390 | 445 | 500 | 50 |  | tuner_hz | 117 |
| 71 | toggle | Tuner | 156 | 557 | 289 | 86 | TUNER | tuner_on | 118 |
| 72 | toggle | Tuner | 496 | 557 | 289 | 86 | MUTE | tuner_mute | 119 |
| 73 | knob | Tuner | 942 | 496 | 116 | 116 | A = | tuner_ref | 120 |
| 74 | readout | Presets | 155 | 145 | 290 | 40 |  | cap_bank | 123 |
| 75 | popup | Presets | 155 | 177 | 290 | 56 |  | pre_src | 124 |
| 76 | list | Presets | 16 | 250 | 1248 | 216 |  | pre | 125 |
| 77 | readout | Presets | 40 | 478 | 1200 | 44 |  | pre_info | 126 |
| 78 | button | Presets | 40 | 560 | 260 | 60 | cap_save | pre_save | 127 |
| 79 | button | Presets | 320 | 560 | 260 | 60 | cap_over | pre_over | 128 |
| 80 | toggle | Presets | 596 | 547 | 289 | 86 | DELETE | pre_del | 129 |
| 81 | knob | Presets | 1092 | 490 | 116 | 116 | SCROLL | pre_scroll | 130 |
| 82 | readout | Presets | 50 | 647 | 900 | 36 |  | status | 131 |
| 83 | readout | Setup | 50 | 495 | 500 | 50 |  | dsp_load | 134 |
| 84 | readout | Setup | 20 | 558 | 1240 | 44 |  | folder | 135 |
| 85 | readout | Setup | 240 | 618 | 800 | 44 |  | version | 136 |
| 86 | list | Pictures | 16 | 165 | 1248 | 408 |  | blk | 143 |
| 87 | readout | Pictures | 30 | 610 | 900 | 60 |  | b_model | 144 |
| 88 | toggle | Pictures | 976 | 589 | 289 | 86 | MOVE | move | 145 |
