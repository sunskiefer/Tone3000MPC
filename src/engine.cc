// THING4000: NAM A2 amps, pedals and IR cabs as an effect plugin for Force / MPC OS (mpc_engine contract).
// Engine reused from the open-source TONE3000 plugin: NeuralAmpModelerCore + the chain / calibration / normalization logic.
// New here: the Force UI model (pages, Q-Links, MOVE), the FFT convolver, resampler, EQ, gate, tuner,
// the preset banks, and an API client (device-code sign-in, search, download) that is compiled OUT unless T3K_ONLINE=1.
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <json.hpp>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "dsp.h"
extern "C" {
#include "engine.h"
#include "chainpics.h"
}
#include "net.h"
#include "param_ids.h"

using json = nlohmann::json;
using namespace t3k;

namespace {

#define T3K_VERSION "0.1.2"
// Online search / download / sign-in is DORMANT: T3K_ONLINE 0 compiles the community API key out and drops every
// network job, so the plugin only loads captures and IRs the user copied into tones/. Build with -DT3K_ONLINE=1 to wake it.
#ifndef T3K_ONLINE
#define T3K_ONLINE 0
#endif
#if T3K_ONLINE
static const char* kDefaultKey = "t3k_pub_Ej7NYu-huVjA5TwLPTb_7xTblnD4ghQG";
#else
static const char* kDefaultKey = "";
#endif
#if T3K_ONLINE
static const char* kDefaultOrigin = "https://www.tone3000.com";
#else
static const char* kDefaultOrigin = "";
#endif
static const int kRate = 44100, kNamRate = 48000, kChunk = 64;
static const int kIrMaxSamples = 24000;          // 0.5 s at 48 kHz
static const int kDryDelay = 96;                 // dry-path delay (44.1 kHz samples) = the wet path's, measured by tools/armtest.cc (impulse test)
#ifndef T3K_PREFILL
#define T3K_PREFILL 96
#endif
static const int kFifoPrefill = T3K_PREFILL;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float db2lin(float d) { return powf(10.0f, d / 20.0f); }

static int find_key(const char* k) {
  static std::map<std::string, int> m;
  if (m.empty()) for (int i = 0; i < P_COUNT; i++) m[P_KEY[i]] = i;
  auto it = m.find(k);
  return it == m.end() ? -1 : it->second;
}

static double now_s() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static double mono_s() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static std::string urlenc(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

static std::string sanitize(const std::string& s, size_t maxlen = 40) {
  std::string o;
  for (unsigned char c : s) {
    if (isalnum(c)) o += (char)c;
    else if (c == ' ' || c == '-' || c == '_' || c == '.') { if (!o.empty() && o.back() != '_') o += '_'; }
    if (o.size() >= maxlen) break;
  }
  return o.empty() ? "x" : o;
}

static std::string slurp(const std::string& path) {
  std::string r;
  if (FILE* f = fopen(path.c_str(), "rb")) {
    char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) r.append(b, n);
    fclose(f);
  }
  return r;
}
static bool spit(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  return ok && rename(tmp.c_str(), path.c_str()) == 0;
}
static void mkdirs(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) {
    cur += p[i];
    if (p[i] == '/' && i > 0) mkdir(cur.c_str(), 0775);
  }
  mkdir(p.c_str(), 0775);
}
static bool ends_with(const std::string& s, const char* suf) {
  size_t n = strlen(suf);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++) if (tolower((unsigned char)s[s.size() - n + i]) != suf[i]) return false;
  return true;
}
static std::string trunc(const std::string& s, size_t n) {
  if (s.size() <= n) return s;
  size_t k = n > 1 ? n - 1 : 1;
  while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;      // never cut a UTF-8 sequence
  return s.substr(0, k) + "~";
}

// ---- factory bank: 8 topologies x 6 voicings = 48 presets; empty slots fill from TONE3000 on first recall ---------
struct Slot { const char* gear; const char* fmt; int rank_off; float level; float mix; };
struct Topo { const char* name; int n; Slot s[4]; bool gate; };
static const Topo kTopos[8] = {
    {"Amp + Cab", 2, {{"amp", "nam", 0, 0, 100}, {"cab", "ir", 0, 0, 100}}, false},
    {"Full Rig", 1, {{"amp-cab", "nam", 0, 0, 100}}, false},
    {"Drive > Amp > Cab", 3, {{"pedal", "nam", 0, 0, 100}, {"amp", "nam", 0, 0, 100}, {"cab", "ir", 0, 0, 100}}, true},
    {"Drive > Amp > Cab > Room", 4, {{"pedal", "nam", 0, 0, 100}, {"amp", "nam", 0, 0, 100}, {"cab", "ir", 0, 0, 100}, {"space", "ir", 0, -3, 30}}, true},
    {"Two Drives > Amp > Cab", 4, {{"pedal", "nam", 0, 0, 100}, {"pedal", "nam", 1, 0, 100}, {"amp", "nam", 0, 0, 100}, {"cab", "ir", 0, 0, 100}}, true},
    {"Amp > Cab > FX", 3, {{"amp", "nam", 0, 0, 100}, {"cab", "ir", 0, 0, 100}, {"outboard", "nam", 0, 0, 100}}, false},
    {"Full Rig > Room", 2, {{"amp-cab", "nam", 0, 0, 100}, {"space", "ir", 0, -3, 30}}, false},
    {"Drive > Rig > FX > Room", 4, {{"pedal", "nam", 0, 0, 100}, {"amp-cab", "nam", 0, 0, 100}, {"outboard", "nam", 0, 0, 100}, {"space", "ir", 0, -3, 30}}, false},
};
struct Voice { const char* name; const char* sort; int rank; };
static const Voice kVoices[6] = {{"Top Trending", "trending", 0}, {"All-Time Pick", "downloads-all-time", 0},
                                 {"Fresh", "newest", 0},          {"Runner-up", "trending", 1},
                                 {"Fan Favourite", "downloads-all-time", 1}, {"Deep Cut", "downloads-all-time", 3}};
static const int kFactoryCount = 48;

static const char* kGearIds[] = {"", "amp-cab", "amp", "cab", "pedal", "outboard", "space", "experimental"};
static const char* kFmtIds[] = {"", "nam", "ir"};
static const char* kSortIds[] = {"trending", "downloads-all-time", "newest", "oldest"};

// ---- runtime objects ---------------------------------------------------------------------------------------------
struct NamRt {
  std::unique_ptr<nam::DSP> dsp;
  bool hasIn = false, hasOut = false, hasLoud = false;
  double inL = 0, outL = 0, loud = 0;
};
struct IrRt {
  Convolver conv;
  float pad = 1.0f;
  float defMix = 100.0f;
  bool trunc = false;
};

struct EqPar {
  bool on = false;
  float low = 0, mid = 0, midf = 0.5f, high = 0, hpf = 0, lpf = 1;
};
static double midf_hz(float x) { return 200.0 * pow(25.0, x); }                 // 200 Hz .. 5 kHz
static double hpf_hz(float x) { return x <= 0.0f ? 0.0 : 20.0 * pow(20.0, x); }  // off, 20 .. 400 Hz
static double lpf_hz(float x) { return x >= 0.98f ? 24000.0 : 2000.0 * pow(10.0, x / 0.98f); }  // 2 kHz .. 20 kHz, off at the top

static void design(EQ& e, const EqPar& p, double fs) {
  e.on = p.on;
  e.design(fs, p.low, p.mid, midf_hz(p.midf), p.high, hpf_hz(p.hpf), lpf_hz(p.lpf));
}

struct Block {
  int type = 0;                 // 0 empty, 1 NAM, 2 IR
  std::string file, label;
  bool bypass = false, loading = false;
  float level = 0, mix = 100;
  EqPar pre_p, post_p;
  EQ pre, post;
  NamRt* nam = nullptr;
  IrRt* ir = nullptr;
  float ingain = 1, outgain = 1;
  int gen = 0;
  std::shared_ptr<std::vector<uint8_t>> pic;   // the picture that came with the capture (straight RGBA, shrunk), or none
  int picw = 0, pich = 0;
  bool pic_dirty = true;                          // not yet handed to the chain picture strip
};

struct Entry {
  std::string title, sub, gear, fmt, file, url;
  long tone_id = 0, model_id = 0;
  int a2 = 0, models = 0;
};

enum { J_LOAD, J_SCAN, J_SEARCH, J_MODELS, J_DOWNLOAD, J_SIGNIN, J_SIGNOUT, J_AUTOFILL };
struct Job {
  int kind = 0, a = 0, b = 0, c = 0;
  std::string s1, s2;
  Entry e;
};
struct Done {
  int blk, gen;
  bool fresh;
  std::string file, label;
  int type;
  NamRt* nam;
  IrRt* ir;
  std::shared_ptr<std::vector<uint8_t>> pic;
  int picw = 0, pich = 0;
};

struct S;
void worker_main(S* s);

struct S {
  // ---- ui state (touched only under the wrapper's per-instance lock) ----
  float v[P_COUNT];
  unsigned rev = 1;
  int view = 0, sel = 0;
  double last_nav = -1e9; int nav_ms = 350;   // the < > arrows: one step per tap, a repeat inside nav_ms is ignored
  bool armed = false;
  Block blk[N_BLOCKS];
  EqPar geq;
  std::string cur_preset;
  int online_level = 0;
  std::string data;
  std::string origin, key;

  // ---- dsp ----
  Resampler up, down;
  Gate gate;
  EQ geq_dsp;
  Tuner tuner;
  std::vector<float> inq, outq, fifo;
  float dry[2048];
  int dry_w = 0;
  bool dirty = true;
  double load = 0;
  bool eq_dirty = true;
  float last_in_peak = 0;
  unsigned underruns = 0;

  // ---- shared with the worker (mu) ----
  std::mutex mu;
  std::condition_variable cv;
  std::deque<Job> jobs;
  std::vector<Done> done;
  std::vector<NamRt*> g_nam;
  std::vector<IrRt*> g_ir;
  std::atomic<bool> has_done{false};
  std::vector<Entry> local, tones, models;
  int online_page = 1, online_pages = 1;
  std::string status = "Ready", auth_info = "Signed out", auth_code = "";
  std::map<std::string, std::string> autofill;
  std::vector<std::string> user_presets;
  bool signed_in = false;
  int gen_ctr = 1;
  std::atomic<bool> quit{false};
  struct cp_inst* cp = nullptr;      // the chain picture strip (src/chainpics.c)
  bool pics_dirty = true;
  volatile bool cancel_dl = false, cancel_auth = false;
  std::thread th;
  Net net;
  struct Tokens { std::string access, refresh; double expires = 0; } tok;

  S() {
    for (int i = 0; i < P_COUNT; i++) v[i] = P_DEF[i];
    up.init(kRate, kNamRate);
    down.init(kNamRate, kRate);
    tuner.init(kRate);
    fifo.assign(kFifoPrefill, 0.0f);
    memset(dry, 0, sizeof dry);
  }
  void bump() { rev++; pics_dirty = true; }
};

// =================================================================================================================
// helpers on the shared state
// =================================================================================================================
static void set_status(S* s, const std::string& t) { std::lock_guard<std::mutex> l(s->mu); s->status = t; s->rev++; }

static std::string tones_dir(S* s) { return s->data + "/tones/"; }
static std::string preset_dir(S* s) { return s->data + "/presets/"; }

static void post_job(S* s, Job j) {
#if !T3K_ONLINE
  if (j.kind == J_SEARCH || j.kind == J_MODELS || j.kind == J_DOWNLOAD || j.kind == J_SIGNIN || j.kind == J_SIGNOUT) return;
#endif
  { std::lock_guard<std::mutex> l(s->mu); s->jobs.push_back(std::move(j)); }
  s->cv.notify_one();
}

static void save_tokens(S* s) {
  json j; j["access_token"] = s->tok.access; j["refresh_token"] = s->tok.refresh; j["expires_at"] = s->tok.expires;
  spit(s->data + "/auth.json", j.dump());
}
static void load_tokens(S* s) {
  std::string t = slurp(s->data + "/auth.json");
  if (t.empty()) return;
  try {
    json j = json::parse(t);
    s->tok.access = j.value("access_token", "");
    s->tok.refresh = j.value("refresh_token", "");
    s->tok.expires = j.value("expires_at", 0.0);
    s->signed_in = !s->tok.refresh.empty() || !s->tok.access.empty();
    if (s->signed_in) s->auth_info = "Signed in";
  } catch (...) {}
}

// ---- worker side: network --------------------------------------------------------------------------------------
static bool take_tokens(S* s, const json& j) {
  if (!j.contains("access_token")) return false;
  s->tok.access = j.value("access_token", "");
  if (j.contains("refresh_token")) s->tok.refresh = j.value("refresh_token", "");
  s->tok.expires = now_s() + j.value("expires_in", 3600.0);
  s->signed_in = true;
  save_tokens(s);
  return true;
}

static bool refresh_token(S* s) {
  if (s->tok.refresh.empty()) return false;
  std::string body, err;
  int code = s->net.request(s->origin + "/api/v1/oauth/token",
                            "grant_type=refresh_token&refresh_token=" + urlenc(s->tok.refresh) + "&client_id=" + urlenc(s->key), true, "", body, err);
  if (code == 200) { try { return take_tokens(s, json::parse(body)); } catch (...) {} }
  if (code == 400 || code == 401) { s->tok = S::Tokens(); s->signed_in = false; save_tokens(s); std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Signed out"; }
  return false;
}

static bool ensure_token(S* s) {
  if (!s->signed_in) return false;
  if (!s->tok.access.empty() && s->tok.expires - now_s() > 60) return true;
  return refresh_token(s);
}

// GET on the API with the bearer token (one refresh+retry on 401). Returns HTTP status; body filled.
static int api_get(S* s, const std::string& path, std::string& body, std::string& err) {
  if (!s->net.ok()) { err = s->net.why(); return 0; }
  if (!ensure_token(s)) { err = "Sign in on the Setup page first"; return 401; }
  int code = s->net.request(s->origin + path, "", false, s->tok.access, body, err);
  if (code == 401 && refresh_token(s)) code = s->net.request(s->origin + path, "", false, s->tok.access, body, err);
  return code;
}

static std::string json_str(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return "";
  if (j[k].is_string()) return j[k].get<std::string>();
  if (j[k].is_number()) return std::to_string(j[k].get<long long>());
  return "";
}

static Entry tone_entry(const json& t) {
  Entry e;
  e.tone_id = t.value("id", 0L);
  e.title = json_str(t, "title");
  e.gear = json_str(t, "gear");
  e.fmt = json_str(t, "format");
  e.models = t.value("models_count", 0);
  e.a2 = t.value("a2_models_count", 0);
  if (t.contains("user") && t["user"].is_object()) e.sub = json_str(t["user"], "username");
  return e;
}

static const char* gear_label(const std::string& g) {
  if (g == "amp") return "Amp";
  if (g == "amp-cab" || g == "full-rig") return "Rig";
  if (g == "cab") return "Cab";
  if (g == "pedal") return "Pedal";
  if (g == "outboard") return "FX";
  if (g == "space") return "Space";
  if (g == "experimental") return "Exp";
  return "";
}

static void scan_local(S* s) {
  std::vector<Entry> out;
  std::string dir = tones_dir(s);
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* de = readdir(d)) {
      std::string n = de->d_name;
      bool nam = ends_with(n, ".nam"), wav = ends_with(n, ".wav");
      if (!nam && !wav) continue;
      Entry e; e.file = n; e.fmt = nam ? "nam" : "ir";
      e.title = n.substr(0, n.size() - 4);
      std::string side = slurp(dir + n.substr(0, n.size() - 4) + ".json");
      if (!side.empty()) {
        try {
          json j = json::parse(side);
          std::string t = json_str(j, "title"), m = json_str(j, "model");
          e.gear = json_str(j, "gear"); e.sub = json_str(j, "user");
          if (!t.empty()) e.title = m.empty() || m == t ? t : t + " - " + m;
        } catch (...) {}
      }
      out.push_back(std::move(e));
    }
    closedir(d);
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.title < b.title; });
  std::lock_guard<std::mutex> l(s->mu);
  s->local.swap(out);
  s->rev++;
}

static void scan_presets(S* s) {
  std::vector<std::string> out;
  if (DIR* d = opendir(preset_dir(s).c_str())) {
    while (dirent* de = readdir(d)) {
      std::string n = de->d_name;
      if (ends_with(n, ".t3p")) out.push_back(n.substr(0, n.size() - 4));
    }
    closedir(d);
  }
  std::sort(out.begin(), out.end());
  std::lock_guard<std::mutex> l(s->mu);
  s->user_presets.swap(out);
  s->rev++;
}

static void model_label(S* s, const std::string& file, std::string& label) {
  label = file;
  size_t dot = label.rfind('.');
  if (dot != std::string::npos) label.resize(dot);
  std::lock_guard<std::mutex> l(s->mu);
  for (auto& e : s->local) if (e.file == file) { label = e.title; break; }
}

// Build the runtime object for one file (worker thread). Returns false with text in err.
static bool build_model(S* s, const std::string& file, int quality, Done& d, std::string& err) {
  std::string path = tones_dir(s) + file;
  d.nam = nullptr; d.ir = nullptr;
  if (ends_with(file, ".nam")) {
    try {
      std::unique_ptr<NamRt> rt(new NamRt());
      nam::DspLoadOptions opt;
      opt.prewarm = false;
      rt->dsp = nam::get_dsp(std::filesystem::path(path), opt);
      if (!rt->dsp) { err = "cannot read model"; return false; }
      if (auto* sl = dynamic_cast<nam::SlimmableModel*>(rt->dsp.get())) sl->SetSlimmableSize(quality == 1 ? 0.0 : 1.0);
      rt->dsp->Reset((double)kNamRate, kChunk);
      rt->dsp->prewarm();
      nam::DSP* m = rt->dsp.get();
      rt->hasIn = m->HasInputLevel(); rt->inL = rt->hasIn ? m->GetInputLevel() : 0;
      rt->hasOut = m->HasOutputLevel(); rt->outL = rt->hasOut ? m->GetOutputLevel() : 0;
      rt->hasLoud = m->HasLoudness(); rt->loud = rt->hasLoud ? m->GetLoudness() : 0;
      d.type = 1; d.nam = rt.release();
      return true;
    } catch (const std::exception& ex) { err = std::string("model: ") + ex.what(); return false; }
    catch (...) { err = "model failed to load"; return false; }
  }
  std::vector<float> ir; int sr = 0;
  if (!read_wav(path, ir, sr)) { err = "cannot read IR wav"; return false; }
  if (sr != kNamRate) {
    Resampler rs; rs.init(sr, kNamRate);
    std::vector<float> o(ir.size() * (size_t)kNamRate / sr + 64);
    int n = rs.process(ir.data(), (int)ir.size(), o.data());
    std::vector<float> z(64, 0.0f);
    n += rs.process(z.data(), 64, o.data() + n);       // flush the filter tail
    o.resize(n); ir.swap(o);
  }
  std::unique_ptr<IrRt> rt(new IrRt());
  bool space = false;
  { std::lock_guard<std::mutex> l(s->mu); for (auto& e : s->local) if (e.file == file) space = e.gear == "space"; }
  bool longIr = (int)ir.size() > kNamRate || space;
  if ((int)ir.size() > kIrMaxSamples) {
    rt->trunc = true;
    int fade = 2048;                                    // fade the cut so it does not click
    ir.resize(kIrMaxSamples);
    for (int i = 0; i < fade; i++) ir[kIrMaxSamples - fade + i] *= 1.0f - (float)i / fade;
  }
  rt->conv.load(ir.data(), (int)ir.size());
  rt->pad = longIr ? 1.0f : db2lin(-18.0f);
  rt->defMix = longIr ? 50.0f : 100.0f;
  d.type = 2; d.ir = rt.release();
  return true;
}

// The picture that came with a capture, if any: the same name with .png / .jpg / .jpeg, or the name a sidecar .json gives in "image".
// Decoded and shrunk here on the worker (never on the audio thread).
static void load_picture(S* s, const std::string& file, Done& d) {
  size_t dot = file.rfind('.');
  std::string base = dot == std::string::npos ? file : file.substr(0, dot);
  std::vector<std::string> cand;
  std::string side = slurp(tones_dir(s) + base + ".json");
  if (!side.empty()) {
    try {
      std::string im = json_str(json::parse(side), "image");
      if (!im.empty() && im.find('/') == std::string::npos && im.find("..") == std::string::npos) cand.push_back(im);
    } catch (...) {}
  }
  for (const char* ext : {".png", ".jpg", ".jpeg", ".PNG", ".JPG", ".JPEG"}) cand.push_back(base + ext);
  for (auto& c : cand) {
    std::string path = tones_dir(s) + c;
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) continue;
    int w = 0, h = 0;
    uint8_t* px = cp_decode(path.c_str(), &w, &h);
    if (!px) continue;
    d.pic = std::make_shared<std::vector<uint8_t>>(px, px + (size_t)w * h * 4);
    d.picw = w; d.pich = h;
    cp_free(px);
    return;
  }
}

static void run_load(S* s, const Job& j) {
  Done d; d.blk = j.a; d.gen = j.b; d.fresh = j.c != 0; d.file = j.s1; d.type = 0; d.nam = nullptr; d.ir = nullptr;
  std::string err;
  int q = (int)s->v[P_QUALITY];
  if (!build_model(s, j.s1, q, d, err)) { set_status(s, "Load failed: " + err); d.type = -1; }
  else { model_label(s, j.s1, d.label); load_picture(s, j.s1, d); }
  { std::lock_guard<std::mutex> l(s->mu); s->done.push_back(d); }
  s->has_done = true;
}

static std::string tone_file_base(const Entry& tone, const Entry& model) {
  return std::to_string(tone.tone_id) + "_" + std::to_string(model.model_id) + "_" + sanitize(tone.title, 28);
}

// Download one model into the tones folder; returns the file name or "".
static std::string download_model(S* s, const Entry& tone, const Entry& model, const std::string& fmt) {
  std::string base = tone_file_base(tone, model);
  std::string file = base + (fmt == "ir" ? ".wav" : ".nam");
  std::string dest = tones_dir(s) + file;
  struct stat st;
  if (stat(dest.c_str(), &st) == 0 && st.st_size > 0) return file;
  if (!s->net.ok()) { set_status(s, s->net.why()); return ""; }
  if (!ensure_token(s)) { set_status(s, "Sign in on the Setup page first"); return ""; }
  std::string err;
  set_status(s, "Downloading " + trunc(tone.title, 28));
  s->cancel_dl = false;
  int rc = s->net.download(model.url, s->tok.access, dest, err, &s->cancel_dl);
  if (rc != 0 && err.find("401") != std::string::npos && refresh_token(s)) rc = s->net.download(model.url, s->tok.access, dest, err, &s->cancel_dl);
  if (rc == -2) rc = s->net.download(model.url, s->tok.access, dest, err, &s->cancel_dl);
  if (rc != 0) { set_status(s, "Download failed: " + err); return ""; }
  json j; j["title"] = tone.title; j["model"] = model.title; j["user"] = tone.sub; j["gear"] = tone.gear; j["format"] = fmt;
  j["tone_id"] = tone.tone_id; j["model_id"] = model.model_id;
  spit(tones_dir(s) + base + ".json", j.dump());
  scan_local(s);
  return file;
}

static bool list_models(S* s, const Entry& tone, std::vector<Entry>& out, std::string& err) {
  std::string body;
  int code = api_get(s, "/api/v1/models?tone_id=" + std::to_string(tone.tone_id) + "&page=1&page_size=24", body, err);
  if (code != 200) { if (err.empty()) err = "HTTP " + std::to_string(code); return false; }
  try {
    json j = json::parse(body);
    const json& arr = j.contains("data") ? j["data"] : j;
    for (const json& m : arr) {
      Entry e; e.model_id = m.value("id", 0L); e.title = json_str(m, "name"); e.url = json_str(m, "model_url");
      e.gear = tone.gear; e.fmt = tone.fmt; e.tone_id = tone.tone_id; e.sub = tone.title; e.file = tone.sub;
      e.a2 = m.value("architecture", 0) == 2 ? 1 : 0;
      if (!e.url.empty()) out.push_back(std::move(e));
    }
  } catch (...) { err = "bad reply"; return false; }
  return true;
}

static bool search_tones(S* s, int page, int gear, int fmt, const char* sort, int pageSize, std::vector<Entry>& out, int& pages, std::string& err) {
  std::string path = "/api/v1/tones/search?page=" + std::to_string(page) + "&page_size=" + std::to_string(pageSize) + "&sort=" + sort;
  if (gear > 0) path += std::string("&gears=") + kGearIds[gear];
  if (fmt > 0) path += std::string("&format=") + kFmtIds[fmt];
  std::string body;
  int code = api_get(s, path, body, err);
  if (code != 200) { if (err.empty()) err = "HTTP " + std::to_string(code); return false; }
  try {
    json j = json::parse(body);
    for (const json& t : j["data"]) out.push_back(tone_entry(t));
    pages = std::max(1, j.value("total_pages", 1));
  } catch (...) { err = "bad reply"; return false; }
  return true;
}

static void run_signin(S* s) {
  if (!s->net.ok()) { set_status(s, s->net.why()); return; }
  std::string body, err;
  int code = s->net.request(s->origin + "/api/v1/oauth/device_authorization", "client_id=" + urlenc(s->key), true, "", body, err);
  if (code != 200) { std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Sign-in failed: " + (err.empty() ? "HTTP " + std::to_string(code) : err); s->rev++; return; }
  json j;
  try { j = json::parse(body); } catch (...) { std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Sign-in failed: bad reply"; s->rev++; return; }
  std::string dev = json_str(j, "device_code"), user = json_str(j, "user_code"), uri = json_str(j, "verification_uri");
  int interval = j.value("interval", 5), expires = j.value("expires_in", 600);
  { std::lock_guard<std::mutex> l(s->mu);
    s->auth_code = user;
    size_t p = uri.find("://"); std::string shortu = p == std::string::npos ? uri : uri.substr(p + 3);
    s->auth_info = "Open " + shortu + " and enter the code"; s->rev++; }
  s->cancel_auth = false;
  double t0 = mono_s();
  while (!s->quit && !s->cancel_auth && mono_s() - t0 < expires) {
    for (int i = 0; i < interval * 10 && !s->quit && !s->cancel_auth; i++) usleep(100000);
    if (s->quit || s->cancel_auth) break;
    std::string b2, e2;
    int c2 = s->net.request(s->origin + "/api/v1/oauth/token",
                            "grant_type=" + urlenc("urn:ietf:params:oauth:grant-type:device_code") + "&device_code=" + urlenc(dev) + "&client_id=" + urlenc(s->key), true, "", b2, e2);
    json r;
    try { r = json::parse(b2); } catch (...) {}
    if (c2 == 200 && take_tokens(s, r)) {
      std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Signed in"; s->auth_code.clear(); s->status = "Signed in"; s->rev++;
      return;
    }
    std::string er = r.is_object() ? json_str(r, "error") : "";
    if (er == "authorization_pending" || (er.empty() && c2 == 0)) continue;
    if (er == "slow_down") { interval += 5; continue; }
    std::lock_guard<std::mutex> l(s->mu);
    s->auth_info = er == "access_denied" ? "Sign-in was denied" : er == "expired_token" ? "Code expired, try again" : "Sign-in failed";
    s->auth_code.clear(); s->rev++;
    return;
  }
  std::lock_guard<std::mutex> l(s->mu);
  if (!s->signed_in) { s->auth_info = "Sign-in cancelled or timed out"; s->auth_code.clear(); s->rev++; }
}

static void save_autofill(S* s) {
  json j = json::object();
  for (auto& kv : s->autofill) j[kv.first] = kv.second;
  spit(s->data + "/autofill.json", j.dump());
}

// One factory-template slot: find or fetch the file. Returns the file name or "".
static std::string autofill_slot(S* s, const Slot& sl, const Voice& vo, int rank, bool allowNet) {
  std::string key = std::string(sl.gear) + "|" + sl.fmt + "|" + vo.sort + "|" + std::to_string(rank);
  std::string hit;
  { std::lock_guard<std::mutex> l(s->mu); auto it = s->autofill.find(key); if (it != s->autofill.end()) hit = it->second; }
  struct stat st;
  if (!hit.empty() && stat((tones_dir(s) + hit).c_str(), &st) == 0) return hit;
  if (allowNet && s->net.ok() && s->signed_in) {
    int gi = 0, fi = 0;
    for (int i = 0; i < 8; i++) if (!strcmp(kGearIds[i], sl.gear)) gi = i;
    for (int i = 0; i < 3; i++) if (!strcmp(kFmtIds[i], sl.fmt)) fi = i;
    std::vector<Entry> res; int pages = 1; std::string err;
    int ps = std::max(12, rank + 1);
    if (search_tones(s, 1, gi, fi, vo.sort, ps, res, pages, err) && (int)res.size() > 0) {
      const Entry& t = res[std::min<int>(rank, (int)res.size() - 1)];
      std::vector<Entry> ms;
      if (list_models(s, t, ms, err) && !ms.empty()) {
        size_t pick = 0;
        for (size_t i = 0; i < ms.size(); i++) if (ms[i].a2) { pick = i; break; }
        std::string f = download_model(s, t, ms[pick], sl.fmt);
        if (!f.empty()) { std::lock_guard<std::mutex> l(s->mu); s->autofill[key] = f; save_autofill(s); return f; }
      }
    }
  }
  // fall back to the local library: the rank-th capture of that gear / format, else any of that format
  std::lock_guard<std::mutex> l(s->mu);
  std::vector<const Entry*> m, any;
  for (auto& e : s->local) {
    if (e.fmt != sl.fmt) continue;
    any.push_back(&e);
    if (e.gear == sl.gear) m.push_back(&e);
  }
  if (!m.empty()) return m[rank % m.size()]->file;
  if (!any.empty()) return any[rank % any.size()]->file;
  return "";
}

static void run_autofill(S* s, int fidx, int gen0) {
  const Topo& tp = kTopos[fidx / 6];
  const Voice& vo = kVoices[fidx % 6];
  bool allowNet = T3K_ONLINE && s->v[P_PRE_AUTOFILL] > 0.5f;
  for (int i = 0; i < tp.n && !s->quit; i++) {
    set_status(s, "Filling slot " + std::to_string(i + 1) + " of " + std::to_string(tp.n));
    std::string f = autofill_slot(s, tp.s[i], vo, vo.rank + tp.s[i].rank_off, allowNet);
    if (f.empty()) continue;
    Job j; j.kind = J_LOAD; j.a = i; j.b = gen0 + i; j.c = 0; j.s1 = f;
    run_load(s, j);
  }
  set_status(s, "Ready");
}

void worker_main(S* s) {
  for (;;) {
    Job j;
    std::vector<NamRt*> gn; std::vector<IrRt*> gi;
    {
      std::unique_lock<std::mutex> l(s->mu);
      s->cv.wait_for(l, std::chrono::seconds(2), [&] { return s->quit || !s->jobs.empty() || !s->g_nam.empty() || !s->g_ir.empty(); });
      gn.swap(s->g_nam); gi.swap(s->g_ir);
      if (s->quit) { for (auto p : gn) delete p; for (auto p : gi) delete p; return; }
      if (!s->jobs.empty()) { j = std::move(s->jobs.front()); s->jobs.pop_front(); } else j.kind = -1;
    }
    for (auto p : gn) delete p;
    for (auto p : gi) delete p;
    switch (j.kind) {
      case J_LOAD: run_load(s, j); break;
      case J_SCAN: scan_local(s); scan_presets(s); break;
#if T3K_ONLINE
      case J_SEARCH: {
        std::vector<Entry> res; int pages = 1; std::string err;
        set_status(s, "Searching...");
        if (search_tones(s, j.a, j.b, j.c, j.s1.c_str(), 12, res, pages, err)) {
          std::lock_guard<std::mutex> l(s->mu);
          s->tones = std::move(res); s->online_page = j.a; s->online_pages = pages; s->status = s->tones.empty() ? "Nothing found" : "Ready"; s->rev++;
        } else set_status(s, "Search failed: " + err);
        break; }
      case J_MODELS: {
        std::vector<Entry> ms; std::string err;
        set_status(s, "Loading captures...");
        if (list_models(s, j.e, ms, err)) {
          std::lock_guard<std::mutex> l(s->mu);
          s->models = std::move(ms); s->status = s->models.empty() ? "No captures" : "Ready"; s->rev++;
        } else { std::lock_guard<std::mutex> l(s->mu); s->online_level = 0; s->status = "Failed: " + err; s->rev++; }
        break; }
      case J_DOWNLOAD: {
        // j.e = the model, j.s1 = tone title, j.a = block, j.b = gen
        Entry tone; tone.tone_id = j.e.tone_id; tone.title = j.e.sub; tone.gear = j.e.gear; tone.fmt = j.e.fmt; tone.sub = j.e.file;
        std::string f = download_model(s, tone, j.e, j.e.fmt);
        if (!f.empty()) { Job l; l.kind = J_LOAD; l.a = j.a; l.b = j.b; l.c = 1; l.s1 = f; run_load(s, l); set_status(s, "Ready"); }
        break; }
      case J_SIGNIN: run_signin(s); break;
      case J_SIGNOUT: {
        s->tok = S::Tokens(); s->signed_in = false; save_tokens(s);
        std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Signed out"; s->auth_code.clear(); s->rev++;
        break; }
#endif
      case J_AUTOFILL: run_autofill(s, j.a, j.b); break;
      default: break;
    }
  }
}

// =================================================================================================================
// block / chain logic (called under the wrapper lock)
// =================================================================================================================
static int blocks_used(S* s) { int n = 0; for (auto& b : s->blk) if (b.type) n++; return n; }

static void load_file_into(S* s, int bi, const std::string& file, bool fresh) {
  Block& b = s->blk[bi];
  b.type = ends_with(file, ".nam") ? 1 : 2;
  b.file = file;
  b.label = file;
  b.loading = true;
  b.gen = ++s->gen_ctr;
  Job j; j.kind = J_LOAD; j.a = bi; j.b = b.gen; j.c = fresh ? 1 : 0; j.s1 = file;
  post_job(s, j);
  s->dirty = true;
  s->bump();
}

static void clear_block(S* s, int bi) {
  Block& b = s->blk[bi];
  { std::lock_guard<std::mutex> l(s->mu); if (b.nam) s->g_nam.push_back(b.nam); if (b.ir) s->g_ir.push_back(b.ir); }
  s->cv.notify_one();
  b = Block();
  b.gen = ++s->gen_ctr;
  s->dirty = true;
  s->bump();
}

// picks up finished loads (render and display_rev poll call this)
static void apply_done(S* s) {
  if (!s->has_done.load()) return;
  std::vector<Done> d;
  {
    std::unique_lock<std::mutex> l(s->mu, std::try_to_lock);
    if (!l.owns_lock()) return;
    d.swap(s->done);
    s->has_done = false;
  }
  for (auto& x : d) {
    Block& b = s->blk[x.blk];
    if (x.gen != b.gen) {                                   // stale: the slot was changed or cleared meanwhile
      std::lock_guard<std::mutex> l(s->mu);
      if (x.nam) s->g_nam.push_back(x.nam);
      if (x.ir) s->g_ir.push_back(x.ir);
      continue;
    }
    b.loading = false;
    if (x.type < 0) { s->bump(); continue; }              // failed: keep whatever played before
    {
      std::lock_guard<std::mutex> l(s->mu);
      if (b.nam) s->g_nam.push_back(b.nam);
      if (b.ir) s->g_ir.push_back(b.ir);
    }
    b.nam = x.nam; b.ir = x.ir; b.type = x.type; b.file = x.file; b.label = x.label;
    b.pic = x.pic; b.picw = x.picw; b.pich = x.pich; b.pic_dirty = true;
    if (x.fresh && x.ir) b.mix = x.ir->defMix;
    if (x.fresh && x.nam) b.mix = 100;
    s->dirty = true;
    s->bump();
  }
  s->cv.notify_one();
}

// hands the chain picture strip what it shows: which blocks are in use / bypassed, the selected one, and any new pictures.
// Cheap: runs only after something changed (bump()). Called under the wrapper's lock, like everything that touches s->blk.
static void sync_pics(S* s) {
  if (!s->cp || !s->pics_dirty) return;
  s->pics_dirty = false;
  int used[N_BLOCKS], byp[N_BLOCKS];
  for (int i = 0; i < N_BLOCKS; i++) { used[i] = s->blk[i].type != 0; byp[i] = s->blk[i].bypass ? 1 : 0; }
  cp_set_state(s->cp, used, byp, s->sel);
  for (int i = 0; i < N_BLOCKS; i++) {
    Block& b = s->blk[i];
    if (!b.pic_dirty) continue;
    cp_set_picture(s->cp, i, b.pic ? b.pic->data() : nullptr, b.picw, b.pich);
    b.pic_dirty = false;
  }
}

static void refresh_gains(S* s) {
  bool calib = s->v[P_CALIB] > 0.5f, norm = s->v[P_NORM] > 0.5f;
  float cref = s->v[P_CALIB_REF], tgt = s->v[P_NORM_TGT];
  int lastNam = -1;
  for (int i = 0; i < N_BLOCKS; i++) if (s->blk[i].type == 1 && s->blk[i].nam && !s->blk[i].bypass) lastNam = i;
  for (int i = 0; i < N_BLOCKS; i++) {
    Block& b = s->blk[i];
    b.ingain = b.outgain = 1.0f;
    if (b.type != 1 || !b.nam) continue;
    NamRt* r = b.nam;
    if (calib && r->hasIn) b.ingain = db2lin(cref - (float)r->inL);
    bool done = false;
    if (calib && i != lastNam && r->hasOut && r->outL >= -60 && r->outL <= 60) { b.outgain = db2lin((float)r->outL - cref); done = true; }
    if (!done && norm) {
      float loud = (r->hasLoud && r->loud >= -100 && r->loud <= 0) ? (float)r->loud : tgt;
      b.outgain = db2lin(clampf(tgt - loud, -24, 24));
    }
  }
  s->dirty = false;
}

static void refresh_eq(S* s) {
  design(s->geq_dsp, s->geq, kRate);   // global EQ runs at 44.1 kHz after the chain
  for (auto& b : s->blk) { design(b.pre, b.pre_p, kNamRate); design(b.post, b.post_p, kNamRate); }
  s->eq_dirty = false;
}

static void chain_chunk(S* s, float* x) {   // exactly kChunk mono samples at 48 kHz
  float d[kChunk], y[kChunk];
  for (int bi = 0; bi < N_BLOCKS; bi++) {
    Block& b = s->blk[bi];
    if (!b.type || b.bypass) continue;
    if (b.type == 1 && !b.nam) continue;
    if (b.type == 2 && !b.ir) continue;
    memcpy(d, x, sizeof d);
    if (b.pre.on) for (int i = 0; i < kChunk; i++) x[i] = b.pre.tick(x[i]);
    if (b.type == 1) {
      double in[kChunk], out[kChunk];
      for (int i = 0; i < kChunk; i++) in[i] = x[i] * b.ingain;
      NAM_SAMPLE* ip = in; NAM_SAMPLE* op = out;
      b.nam->dsp->process(&ip, &op, kChunk);
      for (int i = 0; i < kChunk; i++) y[i] = (float)out[i] * b.outgain;
    } else {
      b.ir->conv.process(x, y);
      for (int i = 0; i < kChunk; i++) y[i] *= b.ir->pad;
    }
    float lv = db2lin(b.level), m = b.mix * 0.01f;
    for (int i = 0; i < kChunk; i++) x[i] = d[i] * (1.0f - m) + y[i] * lv * m;
    if (b.post.on) for (int i = 0; i < kChunk; i++) x[i] = b.post.tick(x[i]);
  }
}

static void process(void* inst, const int16_t* in, int16_t* out, int frames) {
  S* s = (S*)inst;
#ifdef __arm__
  { unsigned f; asm volatile("vmrs %0, fpscr" : "=r"(f)); f |= 1u << 24; asm volatile("vmsr fpscr, %0" ::"r"(f)); }
#endif
  double t0 = mono_s();
  apply_done(s);
  if (s->dirty) refresh_gains(s);
  if (s->eq_dirty) refresh_eq(s);
  int mode = (int)s->v[P_IN_MODE];
  float ig = db2lin(s->v[P_IN_GAIN]), og = db2lin(s->v[P_OUT_GAIN]), mix = s->v[P_MIX] * 0.01f;
  bool gate = s->v[P_GATE_ON] > 0.5f;
  s->gate.setThr(s->v[P_GATE_THR]);
  float mono[128], gated[128], wet[128], tmp[256];
  for (int i = 0; i < frames; i++) {
    float l = in[2 * i] * (1.0f / 32768.0f), r = in[2 * i + 1] * (1.0f / 32768.0f);
    mono[i] = (mode == 0 ? l : mode == 1 ? r : 0.5f * (l + r)) * ig;
  }
  if (s->v[P_TUNER_ON] > 0.5f) s->tuner.push(mono, frames);
  for (int i = 0; i < frames; i++) {
    s->dry[s->dry_w & 2047] = mono[i];
    s->dry_w++;
    gated[i] = gate ? s->gate.tick(mono[i], kRate) : mono[i];
  }
  int n48 = s->up.process(gated, frames, tmp);
  s->inq.insert(s->inq.end(), tmp, tmp + n48);
  size_t used = 0;
  while (s->inq.size() - used >= (size_t)kChunk) {
    chain_chunk(s, &s->inq[used]);
    s->outq.insert(s->outq.end(), s->inq.begin() + used, s->inq.begin() + used + kChunk);
    used += kChunk;
  }
  if (used) s->inq.erase(s->inq.begin(), s->inq.begin() + used);
  if (!s->outq.empty()) {
    std::vector<float>& o = s->outq;
    static thread_local float tb[512];
    size_t pos = 0;
    while (pos < o.size()) {
      int n = (int)std::min<size_t>(o.size() - pos, 200);
      int got = s->down.process(&o[pos], n, tb);
      s->fifo.insert(s->fifo.end(), tb, tb + got);
      pos += n;
    }
    o.clear();
  }
  bool mute = s->v[P_TUNER_ON] > 0.5f && s->v[P_TUNER_MUTE] > 0.5f;
  size_t have = s->fifo.size();
  if (have < (size_t)frames) s->underruns++;
  for (int i = 0; i < frames; i++) wet[i] = (size_t)i < have ? s->fifo[i] : 0.0f;
  s->fifo.erase(s->fifo.begin(), s->fifo.begin() + std::min<size_t>(have, (size_t)frames));
  for (int i = 0; i < frames; i++) {
    float w = wet[i];
    if (s->geq_dsp.on) w = s->geq_dsp.tick(w);
    float dr = s->dry[(s->dry_w - frames + i - kDryDelay) & 2047];
    float o = (w * mix + dr * (1.0f - mix)) * og;
    if (mute) o = 0;
    o = o > 1.5f ? 1.5f : (o < -1.5f ? -1.5f : o);
    o = o > 0.9f ? 0.9f + (o - 0.9f) * 0.4f : (o < -0.9f ? -0.9f + (o + 0.9f) * 0.4f : o);   // soft knee into the int16 range
    float sc = o * 32767.0f;
    int16_t q = (int16_t)(sc >= 32767.0f ? 32767 : (sc <= -32768.0f ? -32768 : (int)lrintf(sc)));
    out[2 * i] = q; out[2 * i + 1] = q;
  }
  double dt = mono_s() - t0;
  double pct = dt / (frames / (double)kRate) * 100.0;
  s->load += (pct - s->load) * 0.05;
}

// =================================================================================================================
// state text (also the preset file format): "T3K1;k=v;...;b1=type,bypass,level,mix,file;e0=on,low,mid,midf,high,hpf,lpf"
// =================================================================================================================
static void put_eq(std::string& o, const char* tag, const EqPar& e) {
  char t[160];
  snprintf(t, sizeof t, "%s=%d,%g,%g,%g,%g,%g,%g;", tag, e.on ? 1 : 0, e.low, e.mid, e.midf, e.high, e.hpf, e.lpf);
  o += t;
}
static const int kSavedParams[] = {P_IN_MODE, P_IN_GAIN, P_OUT_GAIN, P_MIX, P_GATE_ON, P_GATE_THR, P_CALIB, P_CALIB_REF, P_NORM, P_NORM_TGT,
                                   P_QUALITY, P_TUNER_REF, P_PRE_AUTOFILL, P_SEL};

static std::string serialize(S* s, bool forPreset) {
  std::string o = "T3K1;";
  char t[256];
  for (int p : kSavedParams) {
    if (forPreset && (p == P_TUNER_REF || p == P_PRE_AUTOFILL || p == P_SEL || p == P_QUALITY)) continue;
    snprintf(t, sizeof t, "%s=%g;", P_KEY[p], s->v[p]);
    o += t;
  }
  put_eq(o, "eg", s->geq);
  for (int i = 0; i < N_BLOCKS; i++) {
    Block& b = s->blk[i];
    if (!b.type) continue;
    snprintf(t, sizeof t, "b%d=%d,%d,%g,%g,", i, b.type, b.bypass ? 1 : 0, b.level, b.mix);
    o += t; o += b.file; o += ";";
    char tag[8];
    snprintf(tag, sizeof tag, "p%d", i); put_eq(o, tag, b.pre_p);
    snprintf(tag, sizeof tag, "q%d", i); put_eq(o, tag, b.post_p);
  }
  return o;
}

static void get_eq(const char* v, EqPar& e) {
  int on = 0;
  sscanf(v, "%d,%f,%f,%f,%f,%f,%f", &on, &e.low, &e.mid, &e.midf, &e.high, &e.hpf, &e.lpf);
  e.on = on != 0;
}

static void apply_state(S* s, const char* txt, bool forPreset) {
  if (strncmp(txt, "T3K1;", 5)) return;
  for (int i = 0; i < N_BLOCKS; i++) if (s->blk[i].type || s->blk[i].nam || s->blk[i].ir) clear_block(s, i);
  s->geq = EqPar();
  const char* q = txt + 5;
  while (*q) {
    const char* e = strchr(q, ';');
    if (!e) e = q + strlen(q);
    std::string item(q, e - q);
    size_t eq = item.find('=');
    if (eq != std::string::npos) {
      std::string k = item.substr(0, eq), val = item.substr(eq + 1);
      if (k == "eg") get_eq(val.c_str(), s->geq);
      else if ((k[0] == 'b' || k[0] == 'p' || k[0] == 'q') && k.size() == 2 && isdigit((unsigned char)k[1])) {
        int i = k[1] - '0';
        if (i >= 0 && i < N_BLOCKS) {
          if (k[0] == 'b') {
            int type = 0, byp = 0; float lv = 0, mx = 100; int n = 0;
            if (sscanf(val.c_str(), "%d,%d,%f,%f,%n", &type, &byp, &lv, &mx, &n) >= 4 && n > 0) {
              std::string file = val.substr(n);
              if (!file.empty() && file.find('/') == std::string::npos) {
                load_file_into(s, i, file, false);
                s->blk[i].bypass = byp != 0; s->blk[i].level = lv; s->blk[i].mix = mx;
              }
            }
          } else get_eq(val.c_str(), k[0] == 'p' ? s->blk[i].pre_p : s->blk[i].post_p);
        }
      } else {
        int p = find_key(k.c_str());
        if (p >= 0 && !(forPreset && (p == P_TUNER_REF || p == P_PRE_AUTOFILL || p == P_SEL || p == P_QUALITY))) {
          s->v[p] = clampf((float)atof(val.c_str()), P_MIN[p], P_MAX[p]);
        }
      }
    }
    q = *e ? e + 1 : e;
  }
  s->sel = (int)clampf(s->v[P_SEL], 1, N_BLOCKS) - 1;
  s->v[P_SEL] = (float)(s->sel + 1);
  s->armed = false; s->v[P_MOVE] = 0;
  s->dirty = true; s->eq_dirty = true;
  s->bump();
}

// ---- presets ----------------------------------------------------------------------------------------------------
static std::string factory_name(int i) { return std::string(kTopos[i / 6].name) + " - " + kVoices[i % 6].name; }

static void load_factory(S* s, int fi) {
  if (fi < 0 || fi >= kFactoryCount) return;
  const Topo& tp = kTopos[fi / 6];
  for (int i = 0; i < N_BLOCKS; i++) if (s->blk[i].type || s->blk[i].nam || s->blk[i].ir) clear_block(s, i);
  s->geq = EqPar();
  s->v[P_GATE_ON] = tp.gate ? 1 : 0;
  s->v[P_GATE_THR] = -62;
  s->v[P_CALIB] = 0; s->v[P_NORM] = 1; s->v[P_MIX] = 100;
  for (int i = 0; i < tp.n; i++) {
    Block& b = s->blk[i];
    b.type = !strcmp(tp.s[i].fmt, "ir") ? 2 : 1;
    b.label = std::string("(") + tp.s[i].gear + ")";
    b.level = tp.s[i].level; b.mix = tp.s[i].mix;
    b.loading = true;
  }
  Job j; j.kind = J_AUTOFILL; j.a = fi; j.b = s->gen_ctr + 1;
  for (int i = 0; i < tp.n; i++) s->blk[i].gen = j.b + i;      // run_autofill loads slot i with gen j.b + i
  s->gen_ctr += tp.n + 1;
  s->cur_preset = factory_name(fi);
  s->sel = 0; s->v[P_SEL] = 1;
  s->dirty = s->eq_dirty = true;
  s->bump();
  post_job(s, j);
}

static void save_user_preset(S* s, const std::string& name) {
  mkdirs(preset_dir(s));
  spit(preset_dir(s) + name + ".t3p", serialize(s, true));
  scan_presets(s);
  s->cur_preset = name;
  s->bump();
}

static void load_user_preset(S* s, const std::string& name) {
  std::string t = slurp(preset_dir(s) + name + ".t3p");
  if (t.empty()) return;
  apply_state(s, t.c_str(), true);
  s->cur_preset = name;
  s->bump();
}

static std::string next_preset_name(S* s) {
  std::lock_guard<std::mutex> l(s->mu);
  for (int i = 1; i < 100000; i++) {
    char n[32]; snprintf(n, sizeof n, "Preset %03d", i);
    bool used = false;
    for (auto& u : s->user_presets) if (u == n) used = true;
    if (!used) return n;
  }
  return "Preset";
}

// =================================================================================================================
// UI: texts, taps, Q-Links
// =================================================================================================================
static int list_count(S* s, int which) {   // 0 browse, 1 presets
  std::lock_guard<std::mutex> l(s->mu);
  if (which == 1) return s->v[P_PRE_SRC] > 0.5f ? kFactoryCount : (int)s->user_presets.size();
  if (s->v[P_SRC] < 0.5f) {
    int n = 0;
    int gi = (int)s->v[P_GEAR], fi = (int)s->v[P_FMT];
    for (auto& e : s->local) {
      if (fi > 0 && e.fmt != kFmtIds[fi]) continue;
      if (gi > 0 && !e.gear.empty() && e.gear != kGearIds[gi] && !(gi == 1 && e.gear == "full-rig")) continue;
      if (gi > 0 && e.gear.empty()) continue;
      n++;
    }
    return n;
  }
  return s->online_level == 0 ? (int)s->tones.size() : (int)s->models.size() + 1;
}

static std::vector<Entry> local_filtered(S* s) {
  std::vector<Entry> r;
  std::lock_guard<std::mutex> l(s->mu);
  int gi = (int)s->v[P_GEAR], fi = (int)s->v[P_FMT];
  for (auto& e : s->local) {
    if (fi > 0 && e.fmt != kFmtIds[fi]) continue;
    if (gi > 0 && (e.gear.empty() || (e.gear != kGearIds[gi] && !(gi == 1 && e.gear == "full-rig")))) continue;
    r.push_back(e);
  }
  return r;
}

static void block_row_text(S* s, int i, char* buf, int n) {
  Block& b = s->blk[i];
  std::string t = std::to_string(i + 1) + " ";
  if (!b.type) t += "(empty)";
  else {
    if (b.bypass) t += "BYP ";
    t += b.type == 1 ? "NAM " : "IR ";
    t += trunc(b.label, 16);
    if (b.loading) t += " ...";
  }
  snprintf(buf, n, "%s", t.c_str());
}

static std::string fmt_hz(double hz) {
  char t[32];
  if (hz >= 1000) snprintf(t, sizeof t, "%.1f kHz", hz / 1000); else snprintf(t, sizeof t, "%d Hz", (int)lrint(hz));
  return t;
}

static EqPar* eq_target(S* s, bool* ok = nullptr) {
  int sc = (int)s->v[P_EQ_SCOPE];
  if (ok) *ok = true;
  if (sc == 0) return &s->geq;
  Block& b = s->blk[s->sel];
  return sc == 1 ? &b.pre_p : &b.post_p;
}

static float get_value(S* s, int p) {
  switch (p) {
    case P_B_BYPASS: return s->blk[s->sel].bypass ? 1.0f : 0.0f;
    case P_B_LEVEL: return s->blk[s->sel].level;
    case P_B_MIX: return s->blk[s->sel].mix;
    case P_MOVE: return s->armed ? 1.0f : 0.0f;
    case P_EQ_ON: return eq_target(s)->on ? 1.0f : 0.0f;
    case P_EQ_LOW: return eq_target(s)->low;
    case P_EQ_MID: return eq_target(s)->mid;
    case P_EQ_MIDF: return eq_target(s)->midf;
    case P_EQ_HIGH: return eq_target(s)->high;
    case P_G_HPF: return eq_target(s)->hpf;
    case P_G_LPF: return eq_target(s)->lpf;
    default: return s->v[p];
  }
}

static void opt_text(int p, int v, char* buf, int n) {
  const char* t = "";
  switch (p) {
    case P_VIEW: t = VIEW_NAMES[std::max(0, std::min(v, N_VIEWS - 1))]; break;
    case P_GEAR: t = GEAR_NAMES[std::max(0, std::min(v, 7))]; break;
    case P_FMT: t = FMT_NAMES[std::max(0, std::min(v, 2))]; break;
    case P_SORT: t = SORT_NAMES[std::max(0, std::min(v, 3))]; break;
    case P_IN_MODE: t = INMODE_NAMES[std::max(0, std::min(v, 2))]; break;
    case P_EQ_SCOPE: t = SCOPE_NAMES[std::max(0, std::min(v, 2))]; break;
    case P_SRC: t = v ? "Online" : "Local"; break;
    case P_PRE_SRC: t = v ? "Factory" : "User"; break;
    case P_QUALITY: t = v ? "Lite" : "Full"; break;
    case P_IN_SEC: t = v ? "Section" : "Overview"; break;
    default: t = v ? "On" : "Off"; break;
  }
  snprintf(buf, n, "%s", t);
}

static void display(S* s, int p, char* buf, int n) {
  float x = get_value(s, p);
  if (P_NOPTS[p]) { opt_text(p, (int)lroundf(x), buf, n); return; }
  switch (p) {
    case P_B_LEVEL: case P_IN_GAIN: case P_OUT_GAIN: case P_EQ_LOW: case P_EQ_MID: case P_EQ_HIGH: snprintf(buf, n, "%+.1f dB", x); return;
    case P_CALIB_REF: snprintf(buf, n, "%.1f dB", x); return;
    case P_B_MIX: case P_MIX: snprintf(buf, n, "%d%%", (int)lroundf(x)); return;
    case P_GATE_THR: snprintf(buf, n, "%d dB", (int)lroundf(x)); return;
    case P_NORM_TGT: snprintf(buf, n, "%d LUFS", (int)lroundf(x)); return;
    case P_EQ_MIDF: snprintf(buf, n, "%s", fmt_hz(midf_hz(x)).c_str()); return;
    case P_G_HPF: if (x <= 0) snprintf(buf, n, "Off"); else snprintf(buf, n, "%s", fmt_hz(hpf_hz(x)).c_str()); return;
    case P_G_LPF: if (x >= 0.98f) snprintf(buf, n, "Off"); else snprintf(buf, n, "%s", fmt_hz(lpf_hz(x)).c_str()); return;
    case P_TUNER_REF: snprintf(buf, n, "A=%d", (int)lroundf(x)); return;
    case P_SEL: snprintf(buf, n, "Block %d", (int)lroundf(x)); return;
    default: snprintf(buf, n, "%d", (int)lroundf(x)); return;
  }
}

static void set_control(S* s, int p, float x) {
  x = clampf(x, P_MIN[p], P_MAX[p]);
#if !T3K_ONLINE
  if (p == P_SRC || p == P_PRE_AUTOFILL) x = 0;                 // dormant: local files only
#endif
  if (P_NOPTS[p] || P_INT[p]) x = roundf(x);
  switch (p) {
    case P_B_BYPASS: s->blk[s->sel].bypass = x > 0.5f; s->dirty = true; s->bump(); return;
    case P_B_LEVEL: s->blk[s->sel].level = x; return;
    case P_B_MIX: s->blk[s->sel].mix = x; return;
    case P_MOVE: s->armed = x > 0.5f && s->blk[s->sel].type; s->v[P_MOVE] = s->armed ? 1.f : 0.f; s->bump(); return;
    case P_EQ_ON: eq_target(s)->on = x > 0.5f; s->eq_dirty = true; return;
    case P_EQ_LOW: eq_target(s)->low = x; s->eq_dirty = true; return;
    case P_EQ_MID: eq_target(s)->mid = x; s->eq_dirty = true; return;
    case P_EQ_MIDF: eq_target(s)->midf = x; s->eq_dirty = true; return;
    case P_EQ_HIGH: eq_target(s)->high = x; s->eq_dirty = true; return;
    case P_G_HPF: eq_target(s)->hpf = x; s->eq_dirty = true; return;
    case P_G_LPF: eq_target(s)->lpf = x; s->eq_dirty = true; return;
    default: break;
  }
  bool moved = s->v[p] != x;
  s->v[p] = x;
  if (!moved) return;
  switch (p) {
    case P_SEL: s->sel = (int)x - 1; s->bump(); break;
    case P_EQ_SCOPE: case P_SRC: case P_GEAR: case P_FMT: case P_SORT: case P_PRE_SRC: s->bump(); break;
    case P_CALIB: case P_CALIB_REF: case P_NORM: case P_NORM_TGT: s->dirty = true; break;
    case P_QUALITY:
      for (int i = 0; i < N_BLOCKS; i++) if (s->blk[i].type == 1 && !s->blk[i].file.empty()) load_file_into(s, i, s->blk[i].file, false);
      break;
    default: break;
  }
  if (p == P_SRC || p == P_GEAR || p == P_FMT || p == P_SORT) {
    s->v[P_SCROLL] = 0;
    if (s->v[P_SRC] > 0.5f) {                           // online: a new filter means a new search
      s->online_level = 0;
      Job j; j.kind = J_SEARCH; j.a = 1; j.b = (int)s->v[P_GEAR]; j.c = (int)s->v[P_FMT]; j.s1 = kSortIds[(int)s->v[P_SORT]];
      post_job(s, j);
    }
  }
}

static void set_view(S* s, int v) {
  if (v < 0 || v >= N_VIEWS || v == s->view) return;
  s->view = v;
  s->v[P_VIEW] = (float)v;
  s->v[P_IN_SEC] = v > 0 ? 1.f : 0.f;
  cp_set_page(s->cp, v == V_PICS);
  if (v == V_BROWSE) { Job j; j.kind = J_SCAN; post_job(s, j); }
  if (v == V_PRESETS) { Job j; j.kind = J_SCAN; post_job(s, j); }
  s->bump();
}

static void tap_block(S* s, int i) {
  if (s->armed && i != s->sel && s->blk[s->sel].type) {
    std::swap(s->blk[s->sel], s->blk[i]);
    s->blk[s->sel].pic_dirty = s->blk[i].pic_dirty = true;   // the pictures travel with their blocks
    for (int k : {s->sel, i}) {                         // an in-flight load must follow its block
      Block& b = s->blk[k];
      if (b.loading && !b.file.empty()) load_file_into(s, k, b.file, false);
    }
    s->sel = i; s->v[P_SEL] = (float)(i + 1);
    s->armed = false; s->v[P_MOVE] = 0;
    s->dirty = true;
    s->bump();
    return;
  }
  if (s->sel != i) { s->sel = i; s->v[P_SEL] = (float)(i + 1); s->armed = false; s->v[P_MOVE] = 0; s->bump(); }
}

static void step_model(S* s, int dir) {
  Block& b = s->blk[s->sel];
  std::vector<Entry> all;
  { std::lock_guard<std::mutex> l(s->mu); all = s->local; }
  std::vector<const Entry*> m;
  const char* want = b.type == 2 ? "ir" : "nam";
  for (auto& e : all) if (b.type == 0 || e.fmt == want) m.push_back(&e);
  if (m.empty()) { set_status(s, "No captures yet: use Browse to download some"); return; }
  int cur = -1;
  for (size_t i = 0; i < m.size(); i++) if (m[i]->file == b.file) cur = (int)i;
  int nx = cur < 0 ? (dir > 0 ? 0 : (int)m.size() - 1) : (cur + dir + (int)m.size()) % (int)m.size();
  load_file_into(s, s->sel, m[nx]->file, true);
}

static void tap_row(S* s, int row, bool presets) {
  int sc = (int)s->v[presets ? P_PRE_SCROLL : P_SCROLL];
  int idx = sc + row;
  if (presets) {
    if (s->v[P_PRE_SRC] > 0.5f) { load_factory(s, idx); return; }
    std::string name;
    { std::lock_guard<std::mutex> l(s->mu); if (idx < (int)s->user_presets.size()) name = s->user_presets[idx]; }
    if (name.empty()) return;
    if (s->v[P_PRE_DEL] > 0.5f) {
      remove((preset_dir(s) + name + ".t3p").c_str());
      s->v[P_PRE_DEL] = 0;
      scan_presets(s);
      s->bump();
    } else load_user_preset(s, name);
    return;
  }
  if (s->v[P_SRC] < 0.5f) {
    std::vector<Entry> lf = local_filtered(s);
    if (idx >= (int)lf.size()) return;
    if (s->v[P_DEL_MODE] > 0.5f) {
      std::string f = lf[idx].file;
      remove((tones_dir(s) + f).c_str());
      remove((tones_dir(s) + f.substr(0, f.size() - 4) + ".json").c_str());
      s->v[P_DEL_MODE] = 0;
      Job j; j.kind = J_SCAN; post_job(s, j);
      s->bump();
      return;
    }
    load_file_into(s, s->sel, lf[idx].file, true);
    return;
  }
  // online
  Entry e;
  bool back = false;
  {
    std::lock_guard<std::mutex> l(s->mu);
    if (s->online_level == 0) { if (idx >= (int)s->tones.size()) return; e = s->tones[idx]; }
    else {
      if (idx == 0) back = true;
      else if (idx - 1 < (int)s->models.size()) e = s->models[idx - 1];
      else return;
    }
  }
  if (back) { s->online_level = 0; s->v[P_SCROLL] = 0; s->bump(); return; }
  if (s->online_level == 0) {
    s->online_level = 1; s->v[P_SCROLL] = 0;
    { std::lock_guard<std::mutex> l(s->mu); s->models.clear(); }
    Job j; j.kind = J_MODELS; j.e = e; post_job(s, j);
    s->bump();
    return;
  }
  Block& b = s->blk[s->sel];
  b.gen = ++s->gen_ctr;
  b.loading = true;
  b.type = e.fmt == "ir" ? 2 : 1;
  b.label = "downloading...";
  Job j; j.kind = J_DOWNLOAD; j.e = e; j.a = s->sel; j.b = b.gen;
  post_job(s, j);
  s->dirty = true;
  s->bump();
}

static void tap_trigger(S* s, int p) {
  if (p == P_NAV_ZOOM) { set_view(s, V_OVERVIEW); return; }
  if (p == P_NAV_PREV || p == P_NAV_NEXT) {
    double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (t - s->last_nav < s->nav_ms) return;       // a tap that arrives twice must not jump two pages
    s->last_nav = t;
    int v2 = s->view <= 0 ? 1 : s->view + (p == P_NAV_NEXT ? 1 : -1);
    if (v2 < 1) v2 = N_VIEWS - 1;
    if (v2 >= N_VIEWS) v2 = 1;
    set_view(s, v2);
    return;
  }
  if (p >= P_GO_CHAIN && p <= P_GO_SETUP) { set_view(s, 1 + (p - P_GO_CHAIN)); return; }
  if (p == P_GO_PICS) { set_view(s, V_PICS); return; }
  if (p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS) { tap_block(s, p - P_BLK_1); return; }
  if (p >= P_RES_1 && p < P_RES_1 + N_ROWS) { tap_row(s, p - P_RES_1, false); return; }
  if (p >= P_PRE_1 && p < P_PRE_1 + N_ROWS) { tap_row(s, p - P_PRE_1, true); return; }
  switch (p) {
    case P_B_CLEAR: clear_block(s, s->sel); s->armed = false; s->v[P_MOVE] = 0; break;
    case P_B_MODEL_PREV: step_model(s, -1); break;
    case P_B_MODEL_NEXT: step_model(s, 1); break;
    case P_PG_PREV: case P_PG_NEXT: {
      int d = p == P_PG_NEXT ? 1 : -1;
      if (s->v[P_SRC] > 0.5f) {
        int pg = s->online_page + d;
        if (s->online_level == 0 && pg >= 1 && pg <= s->online_pages) {
          Job j; j.kind = J_SEARCH; j.a = pg; j.b = (int)s->v[P_GEAR]; j.c = (int)s->v[P_FMT]; j.s1 = kSortIds[(int)s->v[P_SORT]];
          post_job(s, j);
          s->v[P_SCROLL] = 0;
        }
      } else {
        int c = list_count(s, 0);
        s->v[P_SCROLL] = clampf(s->v[P_SCROLL] + d * N_ROWS, 0, (float)std::max(0, c - N_ROWS));
      }
      s->bump();
      break; }
    case P_REFRESH: { Job j; j.kind = J_SCAN; post_job(s, j);
      if (s->v[P_SRC] > 0.5f) { Job k; k.kind = J_SEARCH; k.a = 1; k.b = (int)s->v[P_GEAR]; k.c = (int)s->v[P_FMT]; k.s1 = kSortIds[(int)s->v[P_SORT]]; s->online_level = 0; post_job(s, k); }
      break; }
    case P_PRE_SAVE: save_user_preset(s, next_preset_name(s)); break;
    case P_PRE_OVER:
      if (s->v[P_PRE_SRC] < 0.5f && !s->cur_preset.empty()) {
        bool user = false;
        { std::lock_guard<std::mutex> l(s->mu); for (auto& u : s->user_presets) if (u == s->cur_preset) user = true; }
        if (user) save_user_preset(s, s->cur_preset); else set_status(s, "Load a user preset first to overwrite it");
      } else set_status(s, "Load a user preset first to overwrite it");
      break;
#if !T3K_ONLINE
    case P_SIGNIN: case P_SIGNOUT: set_status(s, "Online is switched off: copy captures into the tones folder"); break;
#else
    case P_SIGNIN: { Job j; j.kind = J_SIGNIN; post_job(s, j); { std::lock_guard<std::mutex> l(s->mu); s->auth_info = "Contacting TONE3000..."; } s->bump(); break; }
    case P_SIGNOUT: s->cancel_auth = true; { Job j; j.kind = J_SIGNOUT; post_job(s, j); } break;
#endif
    default: break;
  }
  s->bump();
}

// the text params
static int text_param(S* s, int p, char* buf, int n) {
  if (p >= CAP_FIRST && p <= CAP_LAST) return snprintf(buf, n, "%s", CAP_TEXT[p]);
  std::lock_guard<std::mutex> l(s->mu);   // for the worker-owned strings; cheap and brief
  auto put = [&](const std::string& t) { return snprintf(buf, n, "%s", t.c_str()); };
  if (p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS) {
    s->mu.unlock();
    block_row_text(s, p - P_BLK_1, buf, n);
    s->mu.lock();
    return (int)strlen(buf);
  }
  if (p >= P_RES_1 && p < P_RES_1 + N_ROWS) {
    int idx = (int)s->v[P_SCROLL] + (p - P_RES_1);
    int gi = (int)s->v[P_GEAR], fi = (int)s->v[P_FMT];
    if (s->v[P_SRC] < 0.5f) {
      int k = 0;
      for (auto& e : s->local) {
        if (fi > 0 && e.fmt != kFmtIds[fi]) continue;
        if (gi > 0 && (e.gear.empty() || (e.gear != kGearIds[gi] && !(gi == 1 && e.gear == "full-rig")))) continue;
        if (k++ == idx) return put(trunc((e.fmt == "ir" ? "IR " : "NAM ") + e.title, 40));
      }
      return put(" ");
    }
    if (s->online_level == 0) {
      if (idx < (int)s->tones.size()) {
        const Entry& e = s->tones[idx];
        std::string g = gear_label(e.gear);
        return put(trunc((g.empty() ? "" : g + " ") + e.title + (e.sub.empty() ? "" : " - " + e.sub) + (e.a2 > 0 ? " A2" : ""), 44));
      }
      return put(" ");
    }
    if (idx == 0) return put("<  Back to results");
    if (idx - 1 < (int)s->models.size()) return put(trunc(s->models[idx - 1].title + (s->models[idx - 1].a2 ? " A2" : ""), 40));
    return put(" ");
  }
  if (p >= P_PRE_1 && p < P_PRE_1 + N_ROWS) {
    int idx = (int)s->v[P_PRE_SCROLL] + (p - P_PRE_1);
    if (s->v[P_PRE_SRC] > 0.5f) return idx < kFactoryCount ? put(trunc(factory_name(idx), 44)) : put(" ");
    return idx < (int)s->user_presets.size() ? put(trunc(s->user_presets[idx], 44)) : put(" ");
  }
  switch (p) {
    case P_B_MODEL: { Block& b = s->blk[s->sel]; return put(b.type ? trunc(b.label, 36) : "(empty)"); }
    case P_CHAIN_INFO: {
      char t[96];
      if (s->armed) snprintf(t, sizeof t, "MOVE: tap the block to swap with block %d", s->sel + 1);
      else snprintf(t, sizeof t, "Block %d selected  -  %d in chain", s->sel + 1, blocks_used(s));
      return put(t);
    }
    case P_TUNER_NOTE: case P_TUNER_CENTS: case P_TUNER_HZ: {
      double hz = s->tuner.hz();
      if (hz < 30) return put(p == P_TUNER_NOTE ? "--" : p == P_TUNER_CENTS ? "play a note" : "");
      double ref = s->v[P_TUNER_REF];
      double m = 69 + 12 * log2(hz / ref);
      int nn = (int)lround(m);
      double cents = (m - nn) * 100;
      static const char* nm[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      char t[48];
      if (p == P_TUNER_NOTE) snprintf(t, sizeof t, "%s%d", nm[((nn % 12) + 12) % 12], nn / 12 - 1);
      else if (p == P_TUNER_CENTS) snprintf(t, sizeof t, "%+d cents %s", (int)lround(cents), fabs(cents) < 5 ? "IN TUNE" : cents < 0 ? "(flat)" : "(sharp)");
      else snprintf(t, sizeof t, "%.1f Hz", hz);
      return put(t);
    }
    case P_PG_INFO: {
      char t[64];
      if (s->v[P_SRC] > 0.5f) snprintf(t, sizeof t, s->online_level ? "%d captures" : "Page %d of %d", s->online_level ? (int)s->models.size() : s->online_page, s->online_pages);
      else { s->mu.unlock(); int c = list_count(s, 0); s->mu.lock(); snprintf(t, sizeof t, "%d captures", c); }
      return put(t);
    }
    case P_STATUS: case P_OV_STATUS: return put(s->status);
    case P_TGT: { char t[40]; snprintf(t, sizeof t, "Loads into Block %d", s->sel + 1); return put(t); }
    case P_PRE_INFO: return put(s->cur_preset.empty() ? "No preset loaded" : trunc(s->cur_preset, 40));
    case P_AUTH_INFO: return put(s->auth_info);
    case P_AUTH_CODE: return put(s->auth_code.empty() ? "-" : s->auth_code);
    case P_DSP_LOAD: { char t[32]; snprintf(t, sizeof t, "DSP %d%%", (int)lround(s->load)); return put(t); }
    case P_FOLDER: return put(s->data);
    case P_KEY_INFO: return put(s->key == kDefaultKey ? "Community key" : "Custom key");
    case P_VERSION: return put(std::string("THING4000 ") + T3K_VERSION);
    case P_TILE_CHAIN: { int nn = 0, ni = 0; for (auto& b : s->blk) { if (b.type == 1) nn++; if (b.type == 2) ni++; }
      char t[64]; snprintf(t, sizeof t, "CHAIN  %d NAM  %d IR", nn, ni); return put(t); }
    case P_TILE_BROWSE: { char t[64]; snprintf(t, sizeof t, "BROWSE  %d saved", (int)s->local.size()); return put(t); }
    case P_TILE_INOUT: return put(std::string("IN/OUT  ") + INMODE_NAMES[(int)s->v[P_IN_MODE]]);
    case P_TILE_EQ: return put(s->geq.on ? "EQ  GLOBAL ON" : "EQ");
    case P_TILE_TUNER: return put(s->v[P_TUNER_ON] > 0.5f ? "TUNER  ON" : "TUNER");
    case P_TILE_PRESETS: return put(s->cur_preset.empty() ? "PRESETS" : trunc("PRESETS  " + s->cur_preset, 26));
    case P_TILE_SETUP: return put(s->signed_in ? "SETUP  SIGNED IN" : "SETUP");
    case P_TILE_PICS: { int np = 0; for (auto& b : s->blk) if (b.pic) np++; char t[48]; snprintf(t, sizeof t, "PICTURES  %d", np); return put(t); }
    default: return 0;
  }
}

static bool is_text(int p) {
  if (p >= CAP_FIRST && p <= CAP_LAST) return true;
  if ((p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS) || (p >= P_RES_1 && p < P_RES_1 + N_ROWS) || (p >= P_PRE_1 && p < P_PRE_1 + N_ROWS)) return true;
  switch (p) {
    case P_B_MODEL: case P_CHAIN_INFO: case P_TUNER_NOTE: case P_TUNER_CENTS: case P_TUNER_HZ: case P_PG_INFO: case P_STATUS: case P_TGT:
    case P_PRE_INFO: case P_AUTH_INFO: case P_AUTH_CODE: case P_DSP_LOAD: case P_FOLDER: case P_KEY_INFO: case P_VERSION: case P_OV_STATUS:
    case P_TILE_CHAIN: case P_TILE_BROWSE: case P_TILE_INOUT: case P_TILE_EQ: case P_TILE_TUNER: case P_TILE_PRESETS: case P_TILE_SETUP: case P_TILE_PICS: return true;
    default: return false;
  }
}

static bool is_trigger(int p) {
  if (p >= P_NAV_ZOOM && p <= P_GO_SETUP) return true;
  if (p == P_GO_PICS) return true;
  if ((p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS) || (p >= P_RES_1 && p < P_RES_1 + N_ROWS) || (p >= P_PRE_1 && p < P_PRE_1 + N_ROWS)) return true;
  switch (p) {
    case P_B_CLEAR: case P_B_MODEL_PREV: case P_B_MODEL_NEXT: case P_PG_PREV: case P_PG_NEXT: case P_REFRESH: case P_PRE_SAVE: case P_PRE_OVER:
    case P_SIGNIN: case P_SIGNOUT: return true;
    default: return false;
  }
}

// =================================================================================================================
// mpc_engine glue
// =================================================================================================================
void* create(const char* dir) {
  S* s = new S();
  s->data = (dir && *dir) ? dir : "/sdcard/THING4000";
  while (s->data.size() > 1 && s->data.back() == '/') s->data.pop_back();
  mkdirs(s->data); mkdirs(s->data + "/tones"); mkdirs(s->data + "/presets");
  s->origin = kDefaultOrigin; s->key = kDefaultKey;
  std::string st = slurp(s->data + "/settings.json");
  if (!st.empty()) {
    try {
      json j = json::parse(st);
      std::string k = json_str(j, "api_key"), o = json_str(j, "api_domain");
      if (!k.empty()) s->key = k;
      if (!o.empty()) { if (o.find("://") == std::string::npos) o = "https://" + o; while (o.back() == '/') o.pop_back(); s->origin = o; }
    } catch (...) {}
  }
  std::string af = slurp(s->data + "/autofill.json");
  if (!af.empty()) { try { json j = json::parse(af); for (auto it = j.begin(); it != j.end(); ++it) if (it.value().is_string()) s->autofill[it.key()] = it.value(); } catch (...) {} }
  load_tokens(s);
  scan_local(s);
  scan_presets(s);
  s->th = std::thread(worker_main, s);
  s->cp = cp_open(s->data.c_str(), 1);
  s->bump();
  return s;
}

void destroy(void* inst) {
  S* s = (S*)inst;
  s->quit = true; s->cancel_dl = true; s->cancel_auth = true;
  s->cv.notify_all();
  if (s->th.joinable()) s->th.join();
  for (auto& b : s->blk) { delete b.nam; delete b.ir; }
  for (auto& d : s->done) { delete d.nam; delete d.ir; }
  for (auto p : s->g_nam) delete p;
  for (auto p : s->g_ir) delete p;
  cp_close(s->cp);
  delete s;
}

void midi(void*, const uint8_t*, int) {}

static void set_param_(S* s, const char* key, const char* val);
void set_param(void* inst, const char* key, const char* val) {
  S* s = (S*)inst;
  if (s->cp) cp_touch(s->cp);          // the instance the user is working in is the one whose chain the picture strip shows
  set_param_(s, key, val);
  sync_pics(s);
}

static void set_param_(S* s, const char* key, const char* val) {
  float x = (float)atof(val);
  if (!strcmp(key, "dbg_navms")) { s->nav_ms = atoi(val); return; }
  if (!strcmp(key, "state")) { apply_state(s, val, false); return; }
  int p = find_key(key);
  if (p < 0) return;
  if (p >= P_QL1 && p <= P_QL16) {
    int t = VIEW_QLINKS[s->view][p - P_QL1];
    if (t < 0) return;
    set_control(s, t, P_MIN[t] + clampf(x, 0, 1) * (P_MAX[t] - P_MIN[t]));
    s->bump();
    return;
  }
  if (p == P_VIEW || p == P_IN_SEC) return;   // only the page buttons and arrows change the page; a host write-back of the old value must never flip it (tests use go_*)
  if (is_text(p) && !is_trigger(p)) return;
  if (is_trigger(p)) {
    if (x < 0.5f) return;               // the automate-off echo, and a tap arrives as 1
    tap_trigger(s, p);
    return;
  }
  set_control(s, p, x);
  s->bump();
}

int get_param(void* inst, const char* key, char* buf, int n) {
  S* s = (S*)inst;
  sync_pics(s);
  if (s->cp && !strncmp(key, "blk_", 4)) cp_touch(s->cp);   // the Chain page of this instance is being drawn
  if (!strcmp(key, "display_rev")) { apply_done(s); return snprintf(buf, n, "%u", s->rev); }
  if (!strcmp(key, "dbg_underruns")) return snprintf(buf, n, "%u", s->underruns);
  if (!strcmp(key, "dbg_pics")) { int m = 0; for (int i = 0; i < N_BLOCKS; i++) if (s->blk[i].pic) m |= 1 << i; return snprintf(buf, n, "%d", m); }
  if (!strcmp(key, "state")) {
    std::string t = serialize(s, false);
    if ((int)t.size() >= n) return 0;
    return snprintf(buf, n, "%s", t.c_str());
  }
  size_t kl = strlen(key);
  char base[64];
  int suffix = 0;   // 1 = _display, 2 = _name, 3 = _on
  if (kl > 8 && !strcmp(key + kl - 8, "_display")) { suffix = 1; kl -= 8; }
  else if (kl > 5 && !strcmp(key + kl - 5, "_name")) { suffix = 2; kl -= 5; }
  else if (kl > 3 && !strcmp(key + kl - 3, "_on")) { suffix = 3; kl -= 3; }
  if (kl >= sizeof base) return 0;
  memcpy(base, key, kl); base[kl] = 0;
  int p = find_key(base);
  if (p < 0) {
    if (suffix == 3) { p = find_key(key); if (p < 0) return 0; suffix = 0; }       // a parameter really named *_on
    else return 0;
  }
  if (suffix == 3 && !(p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS)) {
    int p2 = find_key(key);                                    // eq_on, gate_on, ... are real parameters
    if (p2 >= 0) { p = p2; suffix = 0; }
  }
  if (p >= P_QL1 && p <= P_QL16) {
    int t = VIEW_QLINKS[s->view][p - P_QL1];
    if (suffix == 2) return snprintf(buf, n, "%s", t >= 0 ? P_NAME[t] : "-");
    if (suffix == 1) { if (t < 0) return snprintf(buf, n, " "); display(s, t, buf, n); return (int)strlen(buf); }
    if (t < 0) return snprintf(buf, n, "0");
    float span = P_MAX[t] - P_MIN[t];
    return snprintf(buf, n, "%g", span > 0 ? (get_value(s, t) - P_MIN[t]) / span : 0.0f);
  }
  if (suffix == 3) {                                          // lit state of a list row
    int lit = 0;
    if (p >= P_BLK_1 && p < P_BLK_1 + N_BLOCKS) lit = (p - P_BLK_1) == s->sel;
    return snprintf(buf, n, "%d", lit);
  }
  if (suffix == 2) {
    if (p == P_B_MODEL) return 0;
    return 0;
  }
  if (is_text(p)) return text_param(s, p, buf, n);
  if (suffix == 1) { display(s, p, buf, n); return (int)strlen(buf); }
  if (is_trigger(p)) return snprintf(buf, n, "0");
  if (p == P_VIEW) return snprintf(buf, n, "%d", s->view);
  if (p == P_IN_SEC) return snprintf(buf, n, "%d", s->view > 0 ? 1 : 0);
  float x = get_value(s, p);
  if (P_NOPTS[p] || P_INT[p]) return snprintf(buf, n, "%d", (int)lroundf(x));
  return snprintf(buf, n, "%g", x);
}

void render(void*, int16_t* out, int frames) { memset(out, 0, sizeof(int16_t) * 2 * frames); }

const mpc_engine_t API = {create, destroy, midi, set_param, get_param, render, process};

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &API; }
