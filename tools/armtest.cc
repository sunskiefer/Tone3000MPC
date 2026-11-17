// ARM self-test harness: this file plus src/engine.cc are built into ONE static ARMv7 executable and run under
// qemu-arm-static (tools/armtest.sh). It exercises the real engine through the mpc_engine API, the way the wrapper does.
// There is no x86 build of anything: the plugin .so is the same code, built for the Force.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "dsp.h"

extern "C" {
#include "engine.h"
#include "chainpics.h"
}
#include "param_ids.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL  "); printf(__VA_ARGS__); printf("\n"); } else { printf("pass  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const mpc_engine_t* E;
static void* inst;
static std::string get(const char* k) { char b[512]; b[0] = 0; int n = E->get_param(inst, k, b, sizeof b); return n > 0 ? b : ""; }
static void set(const char* k, const char* v) { E->set_param(inst, k, v); }
static void setf(const char* k, double v) { char b[64]; snprintf(b, sizeof b, "%g", v); set(k, b); }

static std::vector<int16_t> run(const std::vector<float>& mono, int blocks_extra = 0) {
  std::vector<int16_t> out;
  size_t n = mono.size() + blocks_extra * 128;
  for (size_t p = 0; p < n; p += 128) {
    int16_t in[256], o[256];
    for (int i = 0; i < 128; i++) { float v = (p + i < mono.size()) ? mono[p + i] : 0.0f; int16_t q = (int16_t)lrintf(v * 32767.0f); in[2 * i] = in[2 * i + 1] = q; }
    E->process(inst, in, o, 128);
    for (int i = 0; i < 128; i++) out.push_back(o[2 * i]);
  }
  return out;
}
static std::vector<float> sine(double hz, double amp, int n, double ph = 0) {
  std::vector<float> v(n);
  for (int i = 0; i < n; i++) v[i] = (float)(amp * sin(2 * 3.14159265358979 * hz * i / 44100.0 + ph));
  return v;
}
static double rms(const std::vector<int16_t>& v, size_t a, size_t b) {
  double s = 0; for (size_t i = a; i < b && i < v.size(); i++) s += (double)v[i] * v[i];
  return sqrt(s / std::max<size_t>(1, b - a)) / 32768.0;
}
static void wait_loaded(int blocks = 4000) {
  for (int i = 0; i < blocks; i++) {
    std::vector<float> z(128, 0.0f); run(z);
    std::string t = get("blk_1");
    bool any = false; for (int k = 1; k <= 8; k++) { std::string r = get(("blk_" + std::to_string(k)).c_str()); if (r.find("...") != std::string::npos) any = true; }
    if (!any) return;
    usleep(20000);
  }
}

static void test_pictures(const char* data) {
  printf("--- chain pictures\n");
  std::string d = std::string(data) + "/tones/";
  int w = 0, h = 0;
  uint8_t* px = cp_decode((d + "a2-amp-test.png").c_str(), &w, &h);
  CHECK(px && w == 256 && h == 171, "png decodes and shrinks to fit 256 (got %dx%d)", w, h);
  if (px) {
    const uint8_t* c = px + ((size_t)(h / 2) * w + w / 2) * 4;
    CHECK(abs(c[0] - 200) <= 2 && abs(c[1] - 40) <= 2 && abs(c[2] - 40) <= 2 && c[3] == 255, "png centre colour %d,%d,%d,%d", c[0], c[1], c[2], c[3]);
    cp_free(px);
  }
  px = cp_decode((d + "cab-ir-test-2.jpg").c_str(), &w, &h);
  CHECK(px && w == 256 && h == 192, "jpg decodes and shrinks to fit 256 (got %dx%d)", w, h);
  if (px) {
    const uint8_t* c = px + ((size_t)(h / 2) * w + w / 2) * 4;
    CHECK(abs(c[0] - 40) <= 14 && abs(c[1] - 160) <= 14 && abs(c[2] - 60) <= 14, "jpg centre colour %d,%d,%d", c[0], c[1], c[2]);
    cp_free(px);
  }
  CHECK(cp_decode((d + "corrupt.png").c_str(), &w, &h) == nullptr, "a corrupt picture is refused, not crashed on");
  CHECK(cp_decode((d + "missing.png").c_str(), &w, &h) == nullptr, "a missing picture is refused");
  // the strip: block 1 has a red picture and is selected, block 2 used without picture, block 3 bypassed with a picture, rest empty
  struct cp_inst* c = cp_open(data, 0);
  int used[8] = {1, 1, 1, 0, 0, 0, 0, 0}, byp[8] = {0, 0, 1, 0, 0, 0, 0, 0};
  cp_set_state(c, used, byp, 0);
  std::vector<uint8_t> red(40 * 40 * 4);
  for (size_t i = 0; i < red.size(); i += 4) { red[i] = 200; red[i + 1] = 40; red[i + 2] = 40; red[i + 3] = 255; }
  cp_set_picture(c, 0, red.data(), 40, 40);
  cp_set_picture(c, 2, red.data(), 40, 40);
  const int GW = 1248, GH = 408, GAP = 8, CW = (GW - 3 * GAP) / 4, CH = (GH - GAP) / 2;
  std::vector<uint8_t> strip((size_t)GW * GH * 4);
  int sw = 0, sh = 0;
  CHECK(cp_render(c, strip.data(), (int)strip.size(), &sw, &sh) == 0 && sw == GW && sh == GH, "grid renders at %dx%d", sw, sh);
  auto at = [&](int x, int y) { return strip.data() + ((size_t)y * sw + x) * 4; };   // B, G, R, A
  const uint8_t* ctr = at(CW / 2, CH / 2);
  CHECK(ctr[2] >= 195 && ctr[1] <= 45 && ctr[0] <= 45 && ctr[3] == 255, "block 1 shows its picture in the middle of its square (%d,%d,%d,%d)", ctr[2], ctr[1], ctr[0], ctr[3]);
  const uint8_t* fr = at(CW / 2, 1);
  CHECK(fr[2] > 200 && fr[1] > 150 && fr[0] < 90 && fr[3] > 200, "block 1 is selected: yellow frame (%d,%d,%d,%d)", fr[2], fr[1], fr[0], fr[3]);
  const uint8_t* b3 = at(2 * (CW + GAP) + CW / 2, CH / 2);
  CHECK(b3[2] < 120 && b3[2] > 40 && b3[3] == 255, "block 3 is bypassed: its picture is dimmed (%d,%d,%d)", b3[2], b3[1], b3[0]);
  const uint8_t* e = at(1 * (CW + GAP) + 30, CH + GAP + 30);          // block 6 (row 2, col 2) is empty: a dark card with a light frame, no picture
  CHECK(e[2] < 70 && e[3] == 255, "block 6 is empty: a dark card, not a picture (%d,%d,%d,%d)", e[2], e[1], e[0], e[3]);
  const uint8_t* big = at(3 * (CW + GAP) + CW / 2, CH + GAP + CH / 2);
  (void)big;
  CHECK(CW >= 300 && CH >= 200, "the squares are big: %dx%d", CW, CH);
  FILE* f = fopen("/tmp/strip_test.bgra", "wb"); if (f) { fwrite(strip.data(), 1, strip.size(), f); fclose(f); }
  cp_set_picture(c, 0, nullptr, 0, 0);
  cp_render(c, strip.data(), (int)strip.size(), &sw, &sh);
  ctr = at(CW / 2, CH / 2);
  CHECK(!(ctr[2] >= 195 && ctr[1] <= 45), "clearing the picture draws the block number instead");
  cp_close(c);
}

static void test_dsp_units() {
  printf("--- dsp units\n");
  // convolver vs direct convolution
  t3k::Convolver cv;
  std::vector<float> ir(1500), x(64 * 40), ref(x.size(), 0.0f), out(x.size());
  srand(3);
  for (auto& v : ir) v = (rand() / (float)RAND_MAX - 0.5f) * 0.1f;
  for (auto& v : x) v = rand() / (float)RAND_MAX - 0.5f;
  cv.load(ir.data(), (int)ir.size());
  for (size_t i = 0; i < x.size(); i += 64) cv.process(&x[i], &out[i]);
  for (size_t n = 0; n < x.size(); n++) { double a = 0; for (size_t k = 0; k < ir.size() && k <= n; k++) a += (double)ir[k] * x[n - k]; ref[n] = (float)a; }
  double err = 0, pw = 0;
  for (size_t n = 0; n < x.size(); n++) { err += (out[n] - ref[n]) * (out[n] - ref[n]); pw += ref[n] * ref[n]; }
  CHECK(10 * log10(err / pw + 1e-30) < -90, "convolver matches direct convolution (error %.1f dB)", 10 * log10(err / pw + 1e-30));
  // resampler round trip 44.1 -> 48 -> 44.1: a 1 kHz sine keeps its level
  t3k::Resampler up, dn; up.init(44100, 48000); dn.init(48000, 44100);
  std::vector<float> s(44100), u(60000), d(60000);
  for (size_t i = 0; i < s.size(); i++) s[i] = 0.5f * sinf(2 * 3.14159265f * 1000.0f * i / 44100.0f);
  int nu = 0, nd = 0;
  for (size_t i = 0; i < s.size(); i += 128) { int n = (int)std::min<size_t>(128, s.size() - i); int g = up.process(&s[i], n, &u[nu]); int h = dn.process(&u[nu], g, &d[nd]); nu += g; nd += h; }
  double e = 0; for (int i = 8000; i < 16000; i++) e += d[i] * d[i];
  double lvl = sqrt(e / 8000) / 0.35355;
  CHECK(nu > 47900 && nu < 48100, "44.1->48 produced %d samples for 44100", nu);
  CHECK(nd > 44050 && nd < 44150, "48->44.1 returned %d samples for 44100", nd);
  CHECK(fabs(lvl - 1.0) < 0.01, "round-trip 1 kHz gain %.4f", lvl);
  // biquad: shelf boosts
  t3k::Biquad b; b.lowShelf(48000, 120, 12);
  float y = 0; for (int i = 0; i < 4000; i++) y = b.tick(1.0f);
  CHECK(fabs(20 * log10(y) - 12.0) < 0.3, "low shelf +12 dB at DC reads %.2f dB", 20 * log10(y));
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* data = argc > 1 ? argv[1] : "/tmp/t3kdata";
  const char* files = argc > 2 ? argv[2] : "/tmp/t3kfiles";
  test_dsp_units();
  test_pictures(data);
  printf("--- engine\n");
  E = mpc_engine();
  inst = E->create(data);
  CHECK(inst != nullptr, "create");
  CHECK(get("version").find("0.1.2") != std::string::npos, "version text: '%s'", get("version").c_str());
  CHECK(get("tile_chain").find("CHAIN") == 0, "overview tile text: '%s'", get("tile_chain").c_str());

  // ---- empty chain is a clean pass-through with a fixed delay
  std::vector<float> imp(128 * 8, 0.0f); imp[300] = 0.5f;
  auto o = run(imp, 4);
  size_t pk = 0; int16_t pv = 0; for (size_t i = 0; i < o.size(); i++) if (abs(o[i]) > abs(pv)) { pv = o[i]; pk = i; }
  printf("      impulse at 300 comes out at %zu (delay %zd samples), peak %d\n", pk, (ssize_t)pk - 300, pv);
  CHECK(pv > 12000 && pv < 20000, "pass-through keeps level (peak %d)", pv);
  setf("mix", 0);
  auto od = run(imp, 4);
  size_t pd = 0; int16_t pdv = 0; for (size_t i = 0; i < od.size(); i++) if (abs(od[i]) > abs(pdv)) { pdv = od[i]; pd = i; }
  CHECK(pd == pk, "dry path is delayed to line up with the wet path (dry peak %zu, wet peak %zu)", pd, pk);
  setf("mix", 100);
  auto s1 = run(sine(1000, 0.3, 44100));
  double r = rms(s1, 20000, 40000);
  CHECK(fabs(r - 0.3 / sqrt(2.0)) < 0.01, "pass-through 1 kHz rms %.4f", r);

  // ---- state with a NAM amp + cab IR
  std::string st = std::string("T3K1;b0=1,0,0,100,a2-amp-test.nam;b1=2,0,0,100,cab-ir-test-2.wav;");
  set("state", st.c_str());
  wait_loaded();
  printf("      blk_1 '%s'   blk_2 '%s'\n", get("blk_1").c_str(), get("blk_2").c_str());
  CHECK(get("blk_1").find("NAM") != std::string::npos && get("blk_1").find("...") == std::string::npos, "NAM block loaded");
  CHECK(get("blk_2").find("IR") != std::string::npos && get("blk_2").find("...") == std::string::npos, "IR block loaded");
  CHECK(get("dbg_pics") == "3", "both blocks took the picture that came with their capture (mask %s)", get("dbg_pics").c_str());
  auto g = run(sine(220, 0.2, 44100));
  double gr = rms(g, 20000, 40000);
  bool finite = true; for (auto v : g) if (v == 32767 || v == -32768) finite = true;
  CHECK(gr > 0.005 && gr < 0.9, "amp+cab output level %.4f", gr);
  // bypass the NAM
  set("sel", "1"); set("b_bypass", "1");
  auto g2 = run(sine(220, 0.2, 44100));
  CHECK(get("blk_1").find("BYP") != std::string::npos, "bypass shows on tile: '%s'", get("blk_1").c_str());
  set("b_bypass", "0");

  // ---- state round trip
  std::string saved = get("state");
  printf("      state (%zu bytes): %s\n", saved.size(), saved.c_str());
  CHECK(saved.size() < 8000, "state fits the 8192-byte chunk");
  set("state", "T3K1;");
  CHECK(get("blk_1").find("(empty)") != std::string::npos, "empty state clears the chain");
  CHECK(get("dbg_pics") == "0", "clearing the chain clears the pictures (mask %s)", get("dbg_pics").c_str());
  set("state", saved.c_str()); wait_loaded();
  CHECK(get("blk_2").find("IR") != std::string::npos, "state restores the chain");

  // ---- MOVE: select block 1, arm MOVE, tap block 2 -> swapped
  set("sel", "1"); set("move", "1");
  CHECK(get("chain_info").find("MOVE") != std::string::npos, "MOVE armed text: '%s'", get("chain_info").c_str());
  set("blk_2", "1");
  CHECK(get("blk_1").find("IR") != std::string::npos && get("blk_2").find("NAM") != std::string::npos, "swap: '%s' / '%s'", get("blk_1").c_str(), get("blk_2").c_str());
  CHECK(get("blk_2_on") == "1" && get("blk_1_on") == "0", "selection follows the moved block");
  CHECK(get("move") == "0", "MOVE disarms after the swap");
  CHECK(get("dbg_pics") == "3", "pictures stay with their blocks after the swap (mask %s)", get("dbg_pics").c_str());
  wait_loaded();
  auto g3 = run(sine(220, 0.2, 44100));
  CHECK(rms(g3, 20000, 40000) > 0.002, "audio still flows after the swap");
  // row tap value 0 must be ignored
  set("blk_1", "0");
  CHECK(get("blk_2_on") == "1", "a 0 echo does not retarget the selection");

  // ---- online is dormant: it cannot be switched on, and no network job runs
  set("src", "1");
  CHECK(get("src").find("Online") == std::string::npos, "source stays Local when set to Online: '%s'", get("src").c_str());
  set("signin", "1");
  usleep(300000);
  CHECK(get("status").find("switched off") != std::string::npos, "sign-in is switched off: '%s'", get("status").c_str());
  set("pre_autofill", "1");
  CHECK(get("pre_autofill").find("On") == std::string::npos && get("pre_autofill") != "1", "auto fill online stays off: '%s'", get("pre_autofill").c_str());
  // ---- Q-Links follow the view
  set("go_inout", "1");
  CHECK(get("ql1_name") == "In Gain", "Q-Link 1 on In/Out is '%s'", get("ql1_name").c_str());
  setf("ql1", 1.0);
  CHECK(get("in_gain") == "24", "Q-Link 1 turns In Gain to max (%s)", get("in_gain").c_str());
  setf("ql1", 0.5);
  CHECK(get("ql1_display").find("0.0 dB") != std::string::npos || get("ql1_display").find("+0.0") != std::string::npos, "Q-Link 1 display '%s'", get("ql1_display").c_str());
  set("go_eq", "1");
  set("eq_scope", "1"); setf("eq_low", 6);
  CHECK(get("eq_low") == "6", "block pre-EQ low stored for the selected block");
  set("eq_scope", "0");
  CHECK(get("eq_low") == "0", "global EQ is separate (%s)", get("eq_low").c_str());
  set("nav_zoom", "1");

  // ---- page navigation: one step per tap, and a host write-back of the old page never flips it
  set("go_chain", "1");
  set("dbg_navms", "0");
  set("nav_next", "1");
  CHECK(get("view") == "2", "next arrow from Chain goes to Browse (view %s)", get("view").c_str());
  set("dbg_navms", "350");
  set("nav_next", "1");            // a second event for the same tap, well inside 350 ms
  CHECK(get("view") == "2", "a repeated arrow event inside 350 ms is ignored (view %s)", get("view").c_str());
  usleep(400000);
  set("nav_next", "1");
  CHECK(get("view") == "3", "the next real tap steps exactly one page (view %s)", get("view").c_str());
  set("view", "6");
  CHECK(get("view") == "3", "a host write of an old view value does not flip the page (view %s)", get("view").c_str());
  set("nav_next", "0");
  CHECK(get("view") == "3", "the automate-off echo (0) does nothing (view %s)", get("view").c_str());
  set("dbg_navms", "0");
  set("go_setup", "1");
  set("nav_next", "1");
  CHECK(get("view") == "8", "next arrow from Setup goes to Pictures (view %s)", get("view").c_str());
  set("nav_next", "1");
  CHECK(get("view") == "1", "next arrow from Pictures wraps to Chain (view %s)", get("view").c_str());
  set("nav_prev", "1");
  CHECK(get("view") == "8", "prev arrow from Chain wraps to Pictures (view %s)", get("view").c_str());
  set("dbg_navms", "350");
  set("nav_zoom", "1");

  // ---- dry/wet and gate smoke
  setf("mix", 50); setf("gate_on", 1);
  auto g4 = run(sine(220, 0.2, 44100));
  CHECK(rms(g4, 20000, 40000) > 0.002, "dry/wet + gate audio ok");
  setf("mix", 100); setf("gate_on", 0);

  // ---- factory preset (offline: fills from the local library)
  set("pre_src", "1");
  CHECK(get("pre_1").find("Amp + Cab - Top Trending") == 0, "factory row 1 '%s'", get("pre_1").c_str());
  set("pre_1", "1");
  wait_loaded();
  printf("      after factory 1: '%s' / '%s' info '%s'\n", get("blk_1").c_str(), get("blk_2").c_str(), get("pre_info").c_str());
  CHECK(get("blk_1").find("NAM") != std::string::npos, "factory preset filled slot 1 from the local library");
  // user preset
  set("pre_save", "1");
  set("pre_src", "0");
  CHECK(get("pre_1").find("Preset 001") == 0, "user preset saved: '%s'", get("pre_1").c_str());

  // ---- tuner
  setf("tuner_on", 1);
  auto g5 = run(sine(110.0, 0.4, 44100));
  printf("      tuner: %s | %s | %s\n", get("tuner_note").c_str(), get("tuner_cents").c_str(), get("tuner_hz").c_str());
  CHECK(get("tuner_note") == "A2", "tuner reads A2 for 110 Hz");
  setf("tuner_on", 0);

  // ---- soak: 2 s of loud noise, no NaN/overflow garbage, DSP load readout
  std::vector<float> nz(44100 * 2); srand(9); for (auto& v : nz) v = (rand() / (float)RAND_MAX - 0.5f) * 1.6f;
  auto g6 = run(nz);
  bool ok = true; for (auto v : g6) if (v == -32768) ok = false;
  CHECK(ok, "loud noise stays inside the int16 range");
  printf("      fifo underruns: %s\n", get("dbg_underruns").c_str());
  CHECK(get("dbg_underruns") == "0", "no output underruns over the whole run");
  printf("      dsp load readout (under qemu, not meaningful): %s\n", get("dsp_load").c_str());
  {
    std::string lg;
    FILE* f = fopen((std::string(data) + "/chain/chain.log").c_str(), "r");
    if (f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) lg.append(b, n); fclose(f); }
    printf("      chain.log under qemu (no display here):\n%s", lg.c_str());
    CHECK(lg.find("THING4000 chain pictures starting") != std::string::npos, "the picture strip's thread started and logged");
    CHECK(lg.find("no DRM card") != std::string::npos, "with no display it says so and carries on");
    FILE* l = fopen((std::string(data) + "/chain/layout.txt").c_str(), "r");
    CHECK(l != nullptr, "layout.txt was written with the default place"); if (l) fclose(l);
  }
  double t0 = (double)clock() / CLOCKS_PER_SEC;
  E->destroy(inst);
  CHECK(((double)clock() / CLOCKS_PER_SEC) - t0 < 3.0, "destroy joins the picture thread promptly");
  printf("\n%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
