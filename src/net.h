// HTTPS on the Force through the system's libcurl (dlopen'ed: nothing is linked, so the plugin still loads where it
// is missing and the Browse page just says so). Only ever called from the worker thread.
#pragma once
#include <dlfcn.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <curl/curl.h>

namespace t3k {

class Net {
 public:
  bool ok() { load(); return ok_; }
  const char* why() const { return why_.c_str(); }

  // GET/POST returning the body. bearer may be empty. form is "a=b&c=d" (already encoded) for POST, empty for GET.
  // Returns the HTTP status (0 = transport error, text in err).
  int request(const std::string& url, const std::string& form, bool post, const std::string& bearer, std::string& body,
              std::string& err) {
    if (!load()) { err = why_; return 0; }
    CURL* c = init_();
    if (!c) { err = "curl init failed"; return 0; }
    body.clear();
    struct curl_slist* hdr = nullptr;
    if (!bearer.empty()) hdr = slist_append_(hdr, ("Authorization: Bearer " + bearer).c_str());
    hdr = slist_append_(hdr, "Accept: application/json");
    setopt_(c, CURLOPT_URL, url.c_str());
    setopt_(c, CURLOPT_HTTPHEADER, hdr);
    if (post) { setopt_(c, CURLOPT_POST, 1L); setopt_(c, CURLOPT_POSTFIELDS, form.c_str()); setopt_(c, CURLOPT_POSTFIELDSIZE, (long)form.size()); }
    setopt_(c, CURLOPT_WRITEFUNCTION, &Net::wr_str);
    setopt_(c, CURLOPT_WRITEDATA, &body);
    setopt_(c, CURLOPT_TIMEOUT, 40L);
    common(c);
    CURLcode rc = perform_(c);
    long code = 0;
    if (rc == CURLE_OK) getinfo_(c, CURLINFO_RESPONSE_CODE, &code);
    else err = strerror_(rc);
    slist_free_(hdr);
    cleanup_(c);
    return rc == CURLE_OK ? (int)code : 0;
  }

  // Download to dest (written to dest+".part", renamed when complete; an existing .part is resumed). 0 = ok.
  int download(const std::string& url, const std::string& bearer, const std::string& dest, std::string& err,
               volatile bool* cancel) {
    if (!load()) { err = why_; return -1; }
    std::string part = dest + ".part";
    long long have = 0;
    if (FILE* f = fopen(part.c_str(), "rb")) { fseek(f, 0, SEEK_END); have = ftell(f); fclose(f); }
    FILE* out = fopen(part.c_str(), have > 0 ? "ab" : "wb");
    if (!out) { err = "cannot write " + part; return -1; }
    CURL* c = init_();
    struct curl_slist* hdr = nullptr;
    if (!bearer.empty()) hdr = slist_append_(hdr, ("Authorization: Bearer " + bearer).c_str());
    setopt_(c, CURLOPT_URL, url.c_str());
    setopt_(c, CURLOPT_HTTPHEADER, hdr);
    setopt_(c, CURLOPT_WRITEFUNCTION, &Net::wr_file);
    setopt_(c, CURLOPT_WRITEDATA, out);
    if (have > 0) setopt_(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)have);
    setopt_(c, CURLOPT_FAILONERROR, 1L);
    setopt_(c, CURLOPT_LOW_SPEED_LIMIT, 1000L);
    setopt_(c, CURLOPT_LOW_SPEED_TIME, 30L);
    cancel_ = cancel;
    setopt_(c, CURLOPT_NOPROGRESS, 0L);
    setopt_(c, CURLOPT_XFERINFOFUNCTION, &Net::xfer);
    setopt_(c, CURLOPT_XFERINFODATA, this);
    common(c);
    CURLcode rc = perform_(c);
    long code = 0;
    getinfo_(c, CURLINFO_RESPONSE_CODE, &code);
    fclose(out);
    slist_free_(hdr);
    cleanup_(c);
    if (rc == 33 /* CURLE_RANGE_ERROR: the server ignored the resume */ || (rc == CURLE_OK && code == 416)) { remove(part.c_str()); err = "retry"; return -2; }
    if (rc != CURLE_OK) { err = strerror_(rc); if (code) { err += " (HTTP " + std::to_string(code) + ")"; } return -1; }
    if (rename(part.c_str(), dest.c_str()) != 0) { err = "rename failed"; return -1; }
    return 0;
  }

 private:
  typedef CURL* (*init_t)();
  typedef CURLcode (*setopt_t)(CURL*, CURLoption, ...);
  typedef CURLcode (*perform_t)(CURL*);
  typedef CURLcode (*getinfo_t)(CURL*, CURLINFO, ...);
  typedef void (*cleanup_t)(CURL*);
  typedef const char* (*strerror_t)(CURLcode);
  typedef struct curl_slist* (*sl_app_t)(struct curl_slist*, const char*);
  typedef void (*sl_free_t)(struct curl_slist*);
  typedef CURLcode (*ginit_t)(long);

  init_t init_ = nullptr; setopt_t setopt_ = nullptr; perform_t perform_ = nullptr; getinfo_t getinfo_ = nullptr;
  cleanup_t cleanup_ = nullptr; strerror_t strerror_ = nullptr; sl_app_t slist_append_ = nullptr; sl_free_t slist_free_ = nullptr;
  bool tried_ = false, ok_ = false;
  std::string why_;
  volatile bool* cancel_ = nullptr;

  static size_t wr_str(char* p, size_t s, size_t n, void* u) {
    std::string* b = (std::string*)u;
    if (b->size() + s * n > (4u << 20)) return 0;
    b->append(p, s * n);
    return s * n;
  }
  static size_t wr_file(char* p, size_t s, size_t n, void* u) { return fwrite(p, s, n, (FILE*)u); }
  static int xfer(void* u, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    Net* t = (Net*)u;
    return (t->cancel_ && *t->cancel_) ? 1 : 0;
  }
  void common(CURL* c) {
    setopt_(c, CURLOPT_FOLLOWLOCATION, 1L);
    setopt_(c, CURLOPT_MAXREDIRS, 6L);
    setopt_(c, CURLOPT_CONNECTTIMEOUT, 15L);
    setopt_(c, CURLOPT_NOSIGNAL, 1L);
    setopt_(c, CURLOPT_USERAGENT, "THING4000");
    static const char* cas[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"};
    for (const char* p : cas) if (FILE* f = fopen(p, "rb")) { fclose(f); setopt_(c, CURLOPT_CAINFO, p); break; }
  }
  bool load() {
    if (tried_) return ok_;
    tried_ = true;
    void* h = nullptr;
    const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so", nullptr};
    for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { why_ = "no libcurl on this Force"; return false; }
    ginit_t gi = (ginit_t)dlsym(h, "curl_global_init");
    init_ = (init_t)dlsym(h, "curl_easy_init"); setopt_ = (setopt_t)dlsym(h, "curl_easy_setopt");
    perform_ = (perform_t)dlsym(h, "curl_easy_perform"); getinfo_ = (getinfo_t)dlsym(h, "curl_easy_getinfo");
    cleanup_ = (cleanup_t)dlsym(h, "curl_easy_cleanup"); strerror_ = (strerror_t)dlsym(h, "curl_easy_strerror");
    slist_append_ = (sl_app_t)dlsym(h, "curl_slist_append"); slist_free_ = (sl_free_t)dlsym(h, "curl_slist_free_all");
    if (!gi || !init_ || !setopt_ || !perform_ || !getinfo_ || !cleanup_ || !strerror_ || !slist_append_ || !slist_free_) { why_ = "libcurl is incomplete"; return false; }
    gi(CURL_GLOBAL_DEFAULT);
    ok_ = true;
    return true;
  }
};

}  // namespace t3k
