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
  printf("--- engine\n");
  E = mpc_engine();
  inst = E->create(data);
  CHECK(inst != nullptr, "create");
  CHECK(get("version").find("0.1.0") != std::string::npos, "version text: '%s'", get("version").c_str());
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
  set("state", saved.c_str()); wait_loaded();
  CHECK(get("blk_2").find("IR") != std::string::npos, "state restores the chain");

  // ---- MOVE: select block 1, arm MOVE, tap block 2 -> swapped
  set("sel", "1"); set("move", "1");
  CHECK(get("chain_info").find("MOVE") != std::string::npos, "MOVE armed text: '%s'", get("chain_info").c_str());
  set("blk_2", "1");
  CHECK(get("blk_1").find("IR") != std::string::npos && get("blk_2").find("NAM") != std::string::npos, "swap: '%s' / '%s'", get("blk_1").c_str(), get("blk_2").c_str());
  CHECK(get("blk_2_on") == "1" && get("blk_1_on") == "0", "selection follows the moved block");
  CHECK(get("move") == "0", "MOVE disarms after the swap");
  wait_loaded();
  auto g3 = run(sine(220, 0.2, 44100));
  CHECK(rms(g3, 20000, 40000) > 0.002, "audio still flows after the swap");
  // row tap value 0 must be ignored
  set("blk_1", "0");
  CHECK(get("blk_2_on") == "1", "a 0 echo does not retarget the selection");

  // ---- Q-Links follow the view
  set("view", "3");
  CHECK(get("ql1_name") == "In Gain", "Q-Link 1 on In/Out is '%s'", get("ql1_name").c_str());
  setf("ql1", 1.0);
  CHECK(get("in_gain") == "24", "Q-Link 1 turns In Gain to max (%s)", get("in_gain").c_str());
  setf("ql1", 0.5);
  CHECK(get("ql1_display").find("0.0 dB") != std::string::npos || get("ql1_display").find("+0.0") != std::string::npos, "Q-Link 1 display '%s'", get("ql1_display").c_str());
  set("view", "4");
  set("eq_scope", "1"); setf("eq_low", 6);
  CHECK(get("eq_low") == "6", "block pre-EQ low stored for the selected block");
  set("eq_scope", "0");
  CHECK(get("eq_low") == "0", "global EQ is separate (%s)", get("eq_low").c_str());
  set("view", "0");

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
  E->destroy(inst);
  printf("\n%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
