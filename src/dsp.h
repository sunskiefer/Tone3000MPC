// Tone3000MPC's own small DSP pieces: streaming polyphase resampler, FFT partitioned convolver, RBJ biquads,
// noise gate, pitch (tuner) detector and a WAV reader. Nothing here allocates while rendering.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>

namespace t3k {

static const double kPi = 3.14159265358979323846;

// ---- streaming rational resampler (windowed sinc) ---------------------------------------------------------------
// Converts at the ratio in_rate/out_rate; build once, process blocks of any size. Output count varies per call.
class Resampler {
 public:
  static const int H = 16;                 // half taps -> 32 taps
  void init(int inRate, int outRate, double cutoffScale = 0.95) {
    int a = inRate, b = outRate;
    while (b) { int t = a % b; a = b; b = t; }
    int g = a;
    A_ = inRate / g;                       // input samples consumed per `den_` outputs
    den_ = outRate / g;
    double fc = 0.5 * std::min(1.0, (double)outRate / (double)inRate) * cutoffScale;   // cycles per input sample
    tab_.assign((size_t)den_ * 2 * H, 0.0f);
    for (int p = 0; p < den_; p++) {
      double frac = (double)p / den_;
      double sum = 0;
      for (int j = -H + 1; j <= H; j++) {
        double t = j - frac;
        double x = 2.0 * fc * t;
        double s = fabs(x) < 1e-9 ? 1.0 : sin(kPi * x) / (kPi * x);
        double w = 0.5 + 0.5 * cos(kPi * t / H);     // Hann; adequate for guitar
        if (fabs(t) >= H) w = 0;
        double v = 2.0 * fc * s * w;
        tab_[(size_t)p * 2 * H + (j + H - 1)] = (float)v;
        sum += v;
      }
      if (sum != 0) for (int j = 0; j < 2 * H; j++) tab_[(size_t)p * 2 * H + j] = (float)(tab_[(size_t)p * 2 * H + j] / sum);
    }
    reset();
  }
  void reset() { buf_.assign(H, 0.0f); posu_ = (long)H * den_; }   // input sample 0 sits at buf_[H]
  // out must hold at least in_n * outRate/inRate + 2 samples
  int process(const float* in, int n, float* out) {
    buf_.insert(buf_.end(), in, in + n);
    int produced = 0;
    for (;;) {
      long base = posu_ / den_;
      if (base + H >= (long)buf_.size()) break;       // needs buf[base-H+1 .. base+H]
      int ph = (int)(posu_ % den_);
      const float* c = &tab_[(size_t)ph * 2 * H];
      const float* s = &buf_[base - H + 1];
      float acc = 0;
      for (int j = 0; j < 2 * H; j++) acc += c[j] * s[j];
      out[produced++] = acc;
      posu_ += A_;
    }
    long drop = posu_ / den_ - H;
    if (drop > 0) { buf_.erase(buf_.begin(), buf_.begin() + drop); posu_ -= drop * den_; }
    return produced;
  }
 private:
  int A_ = 1, den_ = 1;
  std::vector<float> tab_;
  std::vector<float> buf_;
  long posu_ = 0;
};

// ---- FFT (radix-2, in place, complex float) ---------------------------------------------------------------------
struct FFT128 {
  static const int N = 128;
  float cs[N / 2], sn[N / 2];
  uint8_t rev[N];
  FFT128() {
    for (int i = 0; i < N / 2; i++) { cs[i] = (float)cos(2 * kPi * i / N); sn[i] = (float)-sin(2 * kPi * i / N); }
    for (int i = 0; i < N; i++) { int r = 0; for (int b = 0; b < 7; b++) if (i & (1 << b)) r |= 1 << (6 - b); rev[i] = (uint8_t)r; }
  }
  // re/im arrays of N; inverse = conj trick handled by caller flag (scaled by 1/N when inv)
  void run(float* re, float* im, bool inv) const {
    for (int i = 0; i < N; i++) { int j = rev[i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    for (int len = 2; len <= N; len <<= 1) {
      int half = len >> 1, step = N / len;
      for (int i = 0; i < N; i += len)
        for (int k = 0; k < half; k++) {
          float wr = cs[k * step], wi = inv ? -sn[k * step] : sn[k * step];
          int a = i + k, b = a + half;
          float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
    }
    if (inv) for (int i = 0; i < N; i++) { re[i] *= 1.0f / N; im[i] *= 1.0f / N; }
  }
};

// ---- uniform partitioned convolver, block 64 --------------------------------------------------------------------
class Convolver {
 public:
  static const int B = 64, BINS = 65;
  bool load(const float* ir, int len) {
    if (len < 1) return false;
    P_ = (len + B - 1) / B;
    hr_.assign((size_t)P_ * BINS, 0); hi_.assign((size_t)P_ * BINS, 0);
    xr_.assign((size_t)P_ * BINS, 0); xi_.assign((size_t)P_ * BINS, 0);
    float re[128], im[128];
    for (int p = 0; p < P_; p++) {
      memset(re, 0, sizeof re); memset(im, 0, sizeof im);
      for (int i = 0; i < B; i++) { int k = p * B + i; if (k < len) re[i] = ir[k]; }
      fft_.run(re, im, false);
      for (int k = 0; k < BINS; k++) { hr_[(size_t)p * BINS + k] = re[k]; hi_[(size_t)p * BINS + k] = im[k]; }
    }
    memset(prev_, 0, sizeof prev_);
    head_ = 0;
    return true;
  }
  // exactly B samples in, B out
  void process(const float* in, float* out) {
    float re[128], im[128];
    for (int i = 0; i < B; i++) { re[i] = prev_[i]; re[B + i] = in[i]; im[i] = im[B + i] = 0; }
    memcpy(prev_, in, sizeof prev_);
    fft_.run(re, im, false);
    head_ = (head_ + P_ - 1) % P_;                      // newest spectrum at head_
    for (int k = 0; k < BINS; k++) { xr_[(size_t)head_ * BINS + k] = re[k]; xi_[(size_t)head_ * BINS + k] = im[k]; }
    float ar[BINS], ai[BINS];
    memset(ar, 0, sizeof ar); memset(ai, 0, sizeof ai);
    for (int p = 0; p < P_; p++) {
      int q = head_ + p; if (q >= P_) q -= P_;
      const float *xr = &xr_[(size_t)q * BINS], *xi = &xi_[(size_t)q * BINS];
      const float *hr = &hr_[(size_t)p * BINS], *hi = &hi_[(size_t)p * BINS];
      for (int k = 0; k < BINS; k++) { ar[k] += xr[k] * hr[k] - xi[k] * hi[k]; ai[k] += xr[k] * hi[k] + xi[k] * hr[k]; }
    }
    for (int k = 0; k < BINS; k++) { re[k] = ar[k]; im[k] = ai[k]; }
    for (int k = 1; k < 64; k++) { re[128 - k] = re[k]; im[128 - k] = -im[k]; }
    fft_.run(re, im, true);
    for (int i = 0; i < B; i++) out[i] = re[B + i];
  }
  int partitions() const { return P_; }
 private:
  FFT128 fft_;
  int P_ = 0, head_ = 0;
  std::vector<float> hr_, hi_, xr_, xi_;
  float prev_[B];
};

// ---- biquad (RBJ cookbook) --------------------------------------------------------------------------------------
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  bool unity() const { return b0 == 1 && b1 == 0 && b2 == 0 && a1 == 0 && a2 == 0; }
  void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
    b0 = (float)(nb0 / na0); b1 = (float)(nb1 / na0); b2 = (float)(nb2 / na0); a1 = (float)(na1 / na0); a2 = (float)(na2 / na0);
  }
  void ident() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
  void lowShelf(double fs, double f, double dB) { shelf(fs, f, dB, false); }
  void highShelf(double fs, double f, double dB) { shelf(fs, f, dB, true); }
  void shelf(double fs, double f, double dB, bool high) {
    if (fabs(dB) < 0.01) { ident(); return; }
    double A = pow(10.0, dB / 40.0), w = 2 * kPi * f / fs, cw = cos(w), sw = sin(w), al = sw / 2.0 * sqrt(2.0), sA = 2 * sqrt(A) * al;
    if (!high) set(A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sA),
                   (A + 1) + (A - 1) * cw + sA, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sA);
    else set(A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sA),
             (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA);
  }
  void peak(double fs, double f, double dB, double q) {
    if (fabs(dB) < 0.01) { ident(); return; }
    double A = pow(10.0, dB / 40.0), w = 2 * kPi * f / fs, cw = cos(w), al = sin(w) / (2 * q);
    set(1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
  }
  void hp(double fs, double f, double q = 0.7071) {
    double w = 2 * kPi * f / fs, cw = cos(w), al = sin(w) / (2 * q);
    set((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
  }
  void lp(double fs, double f, double q = 0.7071) {
    double w = 2 * kPi * f / fs, cw = cos(w), al = sin(w) / (2 * q);
    set((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
  }
  inline float tick(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// A five-band tone stack: low shelf, mid peak, high shelf, high-pass and low-pass (the latter two optional).
struct EQ {
  bool on = false;
  Biquad lo, mid, hi, hp, lp;
  bool useHp = false, useLp = false;
  void design(double fs, double lowdB, double middB, double midHz, double highdB, double hpHz, double lpHz) {
    lo.lowShelf(fs, 120.0, lowdB);
    mid.peak(fs, midHz, middB, 0.9);
    hi.highShelf(fs, 3500.0, highdB);
    useHp = hpHz > 25.0; if (useHp) hp.hp(fs, hpHz);
    useLp = lpHz < 19000.0; if (useLp) lp.lp(fs, lpHz);
  }
  inline float tick(float x) {
    if (!lo.unity()) x = lo.tick(x);
    if (!mid.unity()) x = mid.tick(x);
    if (!hi.unity()) x = hi.tick(x);
    if (useHp) x = hp.tick(x);
    if (useLp) x = lp.tick(x);
    return x;
  }
};

// ---- noise gate --------------------------------------------------------------------------------------------------
struct Gate {
  float env = 0, gain = 1;
  float thrLin = 0.001f;
  void setThr(double dB) { thrLin = (float)pow(10.0, dB / 20.0); }
  inline float tick(float x, double fs) {
    float a = fabsf(x);
    float k = a > env ? 0.2f : (float)(1.0 - exp(-1.0 / (0.05 * fs)));        // fast attack, 50 ms release of the detector
    env += (a - env) * k;
    float target = env > thrLin ? 1.0f : 0.0f;
    float rate = target > gain ? 0.02f : (float)(1.0 / (0.01 * fs));           // open fast, close in ~10 ms
    gain += (target - gain) * (target > gain ? 0.05f : rate * 8.0f);
    return x * gain;
  }
};

// ---- tuner: autocorrelation pitch on a decimated buffer --------------------------------------------------------
class Tuner {
 public:
  void init(double fs) { fs_ = fs; n_ = 0; hz_ = 0; }
  void push(const float* x, int n) {                          // x at 44.1 kHz; decimate by 4 with a boxcar
    for (int i = 0; i < n; i++) {
      acc_ += x[i];
      if (++dc_ == 4) { dec_[n_ % W] = acc_ * 0.25f; acc_ = 0; dc_ = 0; n_++; if (n_ % 256 == 0) analyse(); }
    }
  }
  double hz() const { return hz_; }
 private:
  static const int W = 1024;
  void analyse() {
    float s[W]; double e = 0;
    for (int i = 0; i < W; i++) { s[i] = dec_[(n_ + i) % W]; e += (double)s[i] * s[i]; }
    if (e / W < 1e-6) { hz_ = 0; return; }
    double rate = fs_ / 4.0;
    int lmin = (int)(rate / 1000.0), lmax = std::min((int)(rate / 40.0), W / 2 - 1);
    double best = 0; int bl = 0;
    static float r[W / 2 + 2];
    for (int l = lmin; l <= lmax; l++) { double a = 0; for (int i = 0; i + l < W; i++) a += (double)s[i] * s[i + l]; r[l] = (float)a; if (a > best) { best = a; bl = l; } }
    if (best < 0.3 * e || bl <= lmin || bl >= lmax) { return; }
    // prefer the shortest strong peak (octave-error guard), then parabolic refinement
    for (int l = lmin + 1; l < bl; l++) if (r[l] > 0.9 * best && r[l] >= r[l - 1] && r[l] >= r[l + 1]) { bl = l; break; }
    double y1 = r[bl - 1], y2 = r[bl], y3 = r[bl + 1], d = y1 - 2 * y2 + y3;
    double off = d != 0 ? 0.5 * (y1 - y3) / d : 0;
    hz_ = rate / (bl + off);
  }
  double fs_ = 44100, hz_ = 0, acc_ = 0;
  int dc_ = 0; long n_ = 0;
  float dec_[W] = {0};
};

// ---- WAV reader (PCM 8/16/24/32, float32; first channel) ---------------------------------------------------------
inline bool read_wav(const std::string& path, std::vector<float>& out, int& rate) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d; uint8_t tmp[65536]; size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) { d.insert(d.end(), tmp, tmp + n); if (d.size() > (64u << 20)) break; }
  fclose(f);
  if (d.size() < 44 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;
  int fmt = 0, ch = 1, bits = 16; rate = 48000;
  size_t pos = 12, dataOff = 0, dataLen = 0;
  auto u16 = [&](size_t o) { return (int)(d[o] | d[o + 1] << 8); };
  auto u32 = [&](size_t o) { return (size_t)(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24); };
  while (pos + 8 <= d.size()) {
    size_t len = u32(pos + 4);
    if (!memcmp(&d[pos], "fmt ", 4) && pos + 24 <= d.size()) { fmt = u16(pos + 8); ch = u16(pos + 10); rate = (int)u32(pos + 12); bits = u16(pos + 22);
      if (fmt == 0xFFFE && len >= 26 && pos + 8 + 26 <= d.size()) fmt = u16(pos + 8 + 24); }
    else if (!memcmp(&d[pos], "data", 4)) { dataOff = pos + 8; dataLen = std::min(len, d.size() - dataOff); break; }
    pos += 8 + len + (len & 1);
  }
  if (!dataOff || ch < 1 || rate < 8000 || rate > 400000) return false;
  int bps = bits / 8;
  if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) return false;
  size_t frames = dataLen / (size_t)(bps * ch);
  out.resize(frames);
  for (size_t i = 0; i < frames; i++) {
    const uint8_t* p = &d[dataOff + i * bps * ch];
    float v;
    if (fmt == 3) { float fv; memcpy(&fv, p, 4); v = fv; }
    else if (bits == 8) v = ((int)p[0] - 128) / 128.0f;
    else if (bits == 16) v = (int16_t)(p[0] | p[1] << 8) / 32768.0f;
    else if (bits == 24) { int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24); v = x / 2147483648.0f; }
    else { int32_t x = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); v = x / 2147483648.0f; }
    out[i] = v;
  }
  return frames > 0;
}

}  // namespace t3k
