#include "async_services.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <curl/curl.h>
#include <deque>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
extern char **environ;
#endif

namespace cpzero {
namespace {
constexpr size_t EVENT_LIMIT = 256, QUEUE_BYTES = 1024 * 1024,
                 BODY_LIMIT = 512 * 1024, POLL_BYTES = 128 * 1024;
std::string quote(const std::string &s) {
  std::string o = "\"";
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
      ++i;
    } else if (c == '\n') {
      o += "\\n";
      ++i;
    } else if (c == '\r') {
      o += "\\r";
      ++i;
    } else if (c == '\t') {
      o += "\\t";
      ++i;
    } else if (c < 32) {
      char b[7];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
      ++i;
    } else if (c < 128) {
      o += c;
      ++i;
    } else {
      size_t n = (c & 0xe0) == 0xc0   ? 2
                 : (c & 0xf0) == 0xe0 ? 3
                 : (c & 0xf8) == 0xf0 ? 4
                                      : 0;
      bool valid = n && i + n <= s.size();
      for (size_t j = 1; valid && j < n; j++)
        valid = ((unsigned char)s[i + j] & 0xc0) == 0x80;
      if (valid && n == 2)
        valid = c >= 0xc2;
      if (valid && n == 3)
        valid = !((c == 0xe0 && (unsigned char)s[i + 1] < 0xa0) ||
                  (c == 0xed && (unsigned char)s[i + 1] >= 0xa0));
      if (valid && n == 4)
        valid = c <= 0xf4 && !((c == 0xf0 && (unsigned char)s[i + 1] < 0x90) ||
                               (c == 0xf4 && (unsigned char)s[i + 1] >= 0x90));
      if (valid) {
        o.append(s, i, n);
        i += n;
      } else {
        o += "\xef\xbf\xbd";
        ++i;
      }
    }
  }
  return o + '"';
}
std::string jsstr(JSContext *c, JSValueConst v) {
  size_t n = 0;
  const char *p = JS_ToCStringLen(c, &n, v);
  if (!p)
    throw std::runtime_error("expected string");
  std::string s(p, n);
  JS_FreeCString(c, p);
  return s;
}
std::string prop(JSContext *c, JSValueConst o, const char *key,
                 const std::string &fallback = "") {
  JSValue v = JS_GetPropertyStr(c, o, key);
  std::string s = fallback;
  if (!JS_IsUndefined(v) && !JS_IsNull(v))
    s = jsstr(c, v);
  JS_FreeValue(c, v);
  return s;
}
int number(JSContext *c, JSValueConst v, int fallback) {
  int32_t n;
  return JS_IsUndefined(v) || JS_ToInt32(c, &n, v) < 0 ? fallback : n;
}
int numProp(JSContext *c, JSValueConst o, const char *k, int fallback) {
  JSValue v = JS_GetPropertyStr(c, o, k);
  int n = number(c, v, fallback);
  JS_FreeValue(c, v);
  return n;
}
bool makePipe(int p[2]) {
  if (pipe(p) != 0)
    return false;
  for (int fd : {p[0], p[1]})
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
      close(p[0]);
      close(p[1]);
      return false;
    }
  return true;
}
bool groupExists(pid_t pgid) {
  if (pgid <= 0)
    return false;
  return kill(-pgid, 0) == 0 || errno == EPERM;
}
void signalGroup(pid_t pgid, int signalNumber) {
  if (groupExists(pgid))
    kill(-pgid, signalNumber);
}
JSValue fail(JSContext *c, const std::string &s) {
  return JS_ThrowInternalError(c, "async service: %s", s.c_str());
}
struct Event {
  std::string json;
  size_t bytes;
};
struct ValueGuard {
  JSContext *ctx;
  JSValue value;
  ValueGuard(JSContext *c, JSValue v) : ctx(c), value(v) {}
  ~ValueGuard() {
    if (!JS_IsUndefined(value) && !JS_IsException(value))
      JS_FreeValue(ctx, value);
  }
  operator JSValueConst() const { return value; }
};
} // namespace
struct AsyncServices::Impl {
  struct Proc {
    pid_t pid = -1;
    pid_t pgid = -1;
    int in = -1, out = -1, err = -1;
    std::string pending, outTail, errTail;
    bool inClosed = false, closeRequested = false, outDone = false,
         errDone = false;
    int exitCode = 0;
    std::chrono::steady_clock::time_point killAt{};
  };
  struct Http {
    CURL *easy = nullptr;
    curl_slist *headers = nullptr;
    uint64_t id = 0;
    std::string body, headerText, error;
    size_t maxBytes = 0;
    bool headerOverflow = false;
    long status = 0;
  };
  std::recursive_mutex m;
  std::thread worker;
  bool stopping = false, overflowed = false;
  uint64_t next = 1;
  size_t queuedBytes = 0;
  std::map<uint64_t, Proc> procs;
  std::map<uint64_t, Http *> requests;
  std::deque<Http *> pendingHttp;
  std::deque<uint64_t> cancelHttp;
  std::deque<Event> events;
  CURLM *multi = nullptr;
  Impl() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    signal(SIGPIPE, SIG_IGN);
    multi = curl_multi_init();
    worker = std::thread([this] { run(); });
  }
  ~Impl() { shutdown(); }
  void push(std::string j) {
    std::lock_guard<std::recursive_mutex> g(m);
    if (overflowed)
      return;
    if (events.size() >= EVENT_LIMIT || queuedBytes + j.size() > QUEUE_BYTES) {
      events.clear();
      queuedBytes = 0;
      overflowed = true;
      for (auto &p : procs)
        if (p.second.pid > 0) {
          signalGroup(p.second.pgid, SIGTERM);
          p.second.killAt =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
      for (auto &h : requests)
        cancelHttp.push_back(h.first);
      return;
    }
    queuedBytes += j.size();
    size_t n = j.size();
    events.push_back({std::move(j), n});
  }
  void procEvent(const char *type, uint64_t id, const std::string &data) {
    push("{\"type\":" + quote(type) + ",\"id\":" + std::to_string(id) +
         ",\"data\":" + quote(data) + "}");
  }
  static size_t bodyCb(char *p, size_t a, size_t b, void *u) {
    Http *h = (Http *)u;
    size_t n = a * b;
    if (h->body.size() + n > h->maxBytes) {
      h->error = "response body exceeds limit";
      return 0;
    }
    h->body.append(p, n);
    return n;
  }
  static size_t headCb(char *p, size_t a, size_t b, void *u) {
    Http *h = (Http *)u;
    size_t n = a * b;
    if (h->headerText.size() + n > 32768) {
      h->headerOverflow = true;
      h->error = "response headers exceed limit";
      return 0;
    }
    h->headerText.append(p, n);
    return n;
  }
  static size_t utf8Prefix(const std::string &s) {
    if (s.empty())
      return 0;
    size_t lead = s.size() - 1;
    while (lead > 0 && ((unsigned char)s[lead] & 0xc0) == 0x80)
      --lead;
    unsigned char c = (unsigned char)s[lead];
    size_t need = c < 0x80             ? 1
                  : (c & 0xe0) == 0xc0 ? 2
                  : (c & 0xf0) == 0xe0 ? 3
                  : (c & 0xf8) == 0xf0 ? 4
                                       : 1;
    return s.size() - lead < need ? lead : s.size();
  }
  void stream(Proc *p, uint64_t id, bool err, const char *b, size_t n) {
    std::string &tail = err ? p->errTail : p->outTail;
    tail.append(b, n);
    size_t safe = utf8Prefix(tail);
    if (safe) {
      procEvent(err ? "process/stderr" : "process/stdout", id,
                tail.substr(0, safe));
      tail.erase(0, safe);
    }
    if (tail.size() > 4) {
      procEvent(err ? "process/stderr" : "process/stdout", id, "\xef\xbf\xbd");
      tail.clear();
    }
  }
  void run() {
    while (true) {
      std::deque<uint64_t> cancels;
      {
        std::lock_guard<std::recursive_mutex> g(m);
        if (stopping)
          break;
        cancels.swap(cancelHttp);
        while (!pendingHttp.empty()) {
          Http *h = pendingHttp.front();
          pendingHttp.pop_front();
          curl_multi_add_handle(multi, h->easy);
        }
      }
      for (uint64_t id : cancels) {
        Http *h = nullptr;
        {
          std::lock_guard<std::recursive_mutex> g(m);
          auto it = requests.find(id);
          if (it != requests.end()) {
            h = it->second;
            requests.erase(it);
          }
        }
        if (h) {
          curl_multi_remove_handle(multi, h->easy);
          if (h->headers)
            curl_slist_free_all(h->headers);
          curl_easy_cleanup(h->easy);
          delete h;
        }
      }
      int running = 0;
      curl_multi_perform(multi, &running);
      int msgs = 0;
      while (CURLMsg *msg = curl_multi_info_read(multi, &msgs)) {
        if (msg->msg != CURLMSG_DONE)
          continue;
        Http *h = nullptr;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &h);
        if (!h)
          continue;
        curl_easy_getinfo(h->easy, CURLINFO_RESPONSE_CODE, &h->status);
        if (msg->data.result != CURLE_OK && h->error.empty())
          h->error = curl_easy_strerror(msg->data.result);
        curl_multi_remove_handle(multi, h->easy);
        std::string ev;
        if (h->error.empty()) {
          std::map<std::string, std::string> hs;
          size_t pos = 0;
          while (pos < h->headerText.size()) {
            size_t end = h->headerText.find('\n', pos);
            if (end == std::string::npos)
              end = h->headerText.size();
            std::string line = h->headerText.substr(pos, end - pos);
            auto c = line.find(':');
            if (c != std::string::npos) {
              std::string k = line.substr(0, c), v = line.substr(c + 1);
              while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
                v.erase(0, 1);
              while (!v.empty() &&
                     (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
                v.pop_back();
              if (k.size() < 128 && v.size() < 2048)
                hs[k] = v;
            }
            pos = end + 1;
          }
          std::string hj = "{";
          bool first = true;
          for (auto &x : hs) {
            if (!first)
              hj += ',';
            first = false;
            hj += quote(x.first) + ":" + quote(x.second);
          }
          hj += '}';
          ev = "{\"type\":\"http/done\",\"id\":" + std::to_string(h->id) +
               ",\"status\":" + std::to_string(h->status) +
               ",\"headers\":" + hj + ",\"body\":" + quote(h->body) + "}";
        } else
          ev = "{\"type\":\"http/error\",\"id\":" + std::to_string(h->id) +
               ",\"error\":" + quote(h->error) + "}";
        if (ev.size() > QUEUE_BYTES)
          ev = "{\"type\":\"http/error\",\"id\":" + std::to_string(h->id) +
               ",\"error\":\"response exceeds event queue limit\"}";
        push(std::move(ev));
        {
          std::lock_guard<std::recursive_mutex> g(m);
          requests.erase(h->id);
        }
        if (h->headers)
          curl_slist_free_all(h->headers);
        curl_easy_cleanup(h->easy);
        delete h;
      }
      std::vector<std::pair<uint64_t, Proc *>> ps;
      {
        std::lock_guard<std::recursive_mutex> g(m);
        for (auto &x : procs)
          ps.push_back({x.first, &x.second});
      }
      for (auto [id, p] : ps) {
        std::lock_guard<std::recursive_mutex> guard(m);
        if (p->in >= 0 && !p->pending.empty()) {
          ssize_t n = write(p->in, p->pending.data(), p->pending.size());
          if (n > 0)
            p->pending.erase(0, (size_t)n);
          else if (errno != EAGAIN && errno != EINTR) {
            push("{\"type\":\"process/error\",\"id\":" + std::to_string(id) +
                 ",\"error\":\"stdin pipe write failed\"}");
            close(p->in);
            p->in = -1;
            p->inClosed = true;
          }
        }
        if (p->in >= 0 && p->closeRequested && p->pending.empty()) {
          close(p->in);
          p->in = -1;
          p->inClosed = true;
        }
        for (int which = 0; which < 2; which++) {
          int &fd = which ? p->err : p->out;
          bool &isDone = which ? p->errDone : p->outDone;
          if (fd < 0)
            continue;
          size_t budget = 4096;
          char b[2048];
          while (budget) {
            ssize_t n = read(fd, b, std::min(sizeof b, budget));
            if (n > 0) {
              stream(p, id, which != 0, b, (size_t)n);
              budget -= (size_t)n;
              continue;
            }
            if (n == 0) {
              std::string &tail = which ? p->errTail : p->outTail;
              if (!tail.empty())
                procEvent(which ? "process/stderr" : "process/stdout", id,
                          "\xef\xbf\xbd");
              close(fd);
              fd = -1;
              isDone = true;
            }
            break;
          }
        }
        int st = 0;
        if (p->pid > 0) {
          pid_t r = waitpid(p->pid, &st, WNOHANG);
          if (r == p->pid) {
            p->exitCode = WIFEXITED(st)
                              ? WEXITSTATUS(st)
                              : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
            p->pid = -1;
            signalGroup(p->pgid, SIGTERM);
            if (groupExists(p->pgid))
              p->killAt = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(500);
          } else if (r < 0 && errno == ECHILD) {
            p->pid = -1;
            p->exitCode = 128;
          }
        }
        if (p->killAt.time_since_epoch().count()) {
          if (!groupExists(p->pgid)) {
            p->killAt = {};
          } else if (std::chrono::steady_clock::now() >= p->killAt) {
            signalGroup(p->pgid, SIGKILL);
            p->killAt = {};
          }
        }
        if (p->pid < 0 && !p->killAt.time_since_epoch().count() && p->outDone &&
            p->errDone) {
          if (p->in >= 0) {
            close(p->in);
            p->in = -1;
          }
          push("{\"type\":\"process/exit\",\"id\":" + std::to_string(id) +
               ",\"code\":" + std::to_string(p->exitCode) + "}");
          procs.erase(id);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  }
  void shutdown() {
    {
      std::lock_guard<std::recursive_mutex> g(m);
      if (stopping)
        return;
      stopping = true;
      for (auto &x : procs)
        if (x.second.pid > 0)
          signalGroup(x.second.pgid, SIGTERM);
    }
    if (worker.joinable())
      worker.join();
    for (auto &x : procs) {
      Proc &p = x.second;
      if (p.pid > 0) {
        signalGroup(p.pgid, SIGKILL);
        waitpid(p.pid, nullptr, 0);
      }
      if (p.in >= 0)
        close(p.in);
      if (p.out >= 0)
        close(p.out);
      if (p.err >= 0)
        close(p.err);
    }
    if (multi) {
      curl_multi_cleanup(multi);
      multi = nullptr;
    }
    for (auto &x : requests) {
      Http *h = x.second;
      if (h->headers)
        curl_slist_free_all(h->headers);
      curl_easy_cleanup(h->easy);
      delete h;
    }
    procs.clear();
    requests.clear();
    pendingHttp.clear();
    curl_global_cleanup();
  }
};
AsyncServices::AsyncServices() : impl_(new Impl) {}
AsyncServices::~AsyncServices() = default;
bool AsyncServices::handles(const std::string &s) const {
  return s == "process" || s == "http" || s == "io";
}
void AsyncServices::shutdown() {
  if (impl_)
    impl_->shutdown();
}
JSValue AsyncServices::invoke(JSContext *c, const std::string &s,
                              const std::string &method,
                              const std::string &args) {
  JSValue a = JS_UNDEFINED, v = JS_UNDEFINED;
  try {
    a = JS_ParseJSON(c, args.c_str(), args.size(), "async args");
    if (JS_IsException(a)) {
      a = JS_UNDEFINED;
      return JS_EXCEPTION;
    }
    auto done = [&](JSValue result) {
      if (!JS_IsUndefined(v)) {
        JS_FreeValue(c, v);
        v = JS_UNDEFINED;
      }
      JS_FreeValue(c, a);
      a = JS_UNDEFINED;
      return result;
    };
    if (s == "io" && method == "poll") {
      std::string out = "[";
      bool first = true;
      std::lock_guard<std::recursive_mutex> g(impl_->m);
      if (impl_->overflowed)
        out += "{\"type\":\"io/error\",\"error\":\"event queue overflow; "
               "native work cancelled\"}";
      else
        for (int i = 0; i < 64 && !impl_->events.empty(); i++) {
          if (!first &&
              out.size() + impl_->events.front().json.size() > POLL_BYTES)
            break;
          if (!first)
            out += ',';
          first = false;
          out += impl_->events.front().json;
          impl_->queuedBytes -= impl_->events.front().bytes;
          impl_->events.pop_front();
        }
      out += ']';
      return done(JS_NewStringLen(c, out.data(), out.size()));
    }
    {
      std::lock_guard<std::recursive_mutex> g(impl_->m);
      if (impl_->stopping)
        throw std::runtime_error("native I/O hub is stopped");
      const bool cleanup =
          (s == "process" && (method == "kill" || method == "closeStdin")) ||
          (s == "http" && method == "cancel");
      if (impl_->overflowed && !cleanup)
        throw std::runtime_error("native I/O hub was cancelled after overflow");
    }
    v = JS_GetPropertyUint32(c, a, 0);
    if (s == "process" && method == "spawn") {
      if (!JS_IsObject(v))
        throw std::runtime_error("spawn expects options");
      {
        std::lock_guard<std::recursive_mutex> g(impl_->m);
        if (impl_->procs.size() >= 16)
          throw std::runtime_error("process limit reached");
      }
      std::string command = prop(c, v, "command");
      if (command.empty() || command.size() > 4096 ||
          command.find('\0') != std::string::npos)
        throw std::runtime_error("invalid command");
      JSValue av = JS_GetPropertyStr(c, v, "args");
      std::vector<std::string> storage{command};
      if (JS_IsArray(av)) {
        uint32_t n = 0;
        JSValue len = JS_GetPropertyStr(c, av, "length");
        JS_ToUint32(c, &n, len);
        JS_FreeValue(c, len);
        if (n > 128) {
          JS_FreeValue(c, av);
          throw std::runtime_error("too many process arguments");
        }
        size_t argBytes = command.size();
        for (uint32_t i = 0; i < n; i++) {
          JSValue x = JS_GetPropertyUint32(c, av, i);
          std::string arg = jsstr(c, x);
          JS_FreeValue(c, x);
          if (arg.find('\0') != std::string::npos || arg.size() > 16384 ||
              argBytes + arg.size() > 65536) {
            JS_FreeValue(c, av);
            throw std::runtime_error("process arguments exceed limits");
          }
          argBytes += arg.size();
          storage.push_back(std::move(arg));
        }
      }
      JS_FreeValue(c, av);
      std::vector<char *> argv;
      for (auto &x : storage)
        argv.push_back(x.data());
      argv.push_back(nullptr);
      int ip[2], op[2], ep[2];
      if (!makePipe(ip))
        throw std::runtime_error("pipe creation failed");
      if (!makePipe(op)) {
        close(ip[0]);
        close(ip[1]);
        throw std::runtime_error("pipe creation failed");
      }
      if (!makePipe(ep)) {
        close(ip[0]);
        close(ip[1]);
        close(op[0]);
        close(op[1]);
        throw std::runtime_error("pipe creation failed");
      }
      posix_spawn_file_actions_t fa;
      posix_spawn_file_actions_init(&fa);
      posix_spawn_file_actions_adddup2(&fa, ip[0], 0);
      posix_spawn_file_actions_adddup2(&fa, op[1], 1);
      posix_spawn_file_actions_adddup2(&fa, ep[1], 2);
      for (int fd : {ip[0], ip[1], op[0], op[1], ep[0], ep[1]})
        posix_spawn_file_actions_addclose(&fa, fd);
      std::string cwd = prop(c, v, "cwd");
      if (cwd.size() > 4096 || cwd.find('\0') != std::string::npos)
        throw std::runtime_error("cwd exceeds limit");
#if defined(__APPLE__) || defined(__linux__)
      if (!cwd.empty())
        posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
#endif
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
      posix_spawnattr_setpgroup(&attr, 0);
      pid_t pid;
      int rc =
          posix_spawnp(&pid, command.c_str(), &fa, &attr, argv.data(), environ);
      posix_spawn_file_actions_destroy(&fa);
      posix_spawnattr_destroy(&attr);
      close(ip[0]);
      close(op[1]);
      close(ep[1]);
      if (rc) {
        close(ip[1]);
        close(op[0]);
        close(ep[0]);
        throw std::runtime_error(strerror(rc));
      }
      for (int fd : {ip[1], op[0], ep[0]})
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
      uint64_t id;
      {
        std::lock_guard<std::recursive_mutex> g(impl_->m);
        id = impl_->next++;
        Impl::Proc p;
        p.pid = pid;
        p.pgid = pid;
        p.in = ip[1];
        p.out = op[0];
        p.err = ep[0];
        impl_->procs.emplace(id, std::move(p));
      }
      return done(JS_NewInt64(c, (int64_t)id));
    }
    if (s == "process" &&
        (method == "write" || method == "closeStdin" || method == "kill")) {
      int64_t id;
      JSValue idv = JS_GetPropertyUint32(c, a, 0);
      JS_ToInt64(c, &id, idv);
      JS_FreeValue(c, idv);
      std::lock_guard<std::recursive_mutex> g(impl_->m);
      auto it = impl_->procs.find((uint64_t)id);
      if (it != impl_->procs.end()) {
        auto &p = it->second;
        if (method == "write") {
          JSValue t = JS_GetPropertyUint32(c, a, 1);
          std::string text = jsstr(c, t);
          JS_FreeValue(c, t);
          if (p.in < 0 || p.closeRequested)
            throw std::runtime_error("process stdin is closed");
          if (p.pending.size() + text.size() > 65536)
            throw std::runtime_error("stdin queue limit exceeded");
          p.pending += text;
        } else if (method == "closeStdin") {
          p.closeRequested = true;
        } else if (p.pgid > 0) {
          signalGroup(p.pgid, SIGTERM);
          p.killAt =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
      }
      return done(JS_NULL);
    }
    if (s == "http" && method == "cancel") {
      int64_t id;
      JS_ToInt64(c, &id, v);
      std::lock_guard<std::recursive_mutex> g(impl_->m);
      if (impl_->requests.count((uint64_t)id))
        impl_->cancelHttp.push_back((uint64_t)id);
      return done(JS_NULL);
    }
    if (s == "http" && method == "start") {
      std::string url = prop(c, v, "url"), mth = prop(c, v, "method", "GET"),
                  body = prop(c, v, "body");
      if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
        throw std::runtime_error("only HTTP(S) URLs are allowed");
      if (url.size() > 4096 || mth.size() > 16 || body.size() > 65536 ||
          (mth != "GET" && mth != "POST" && mth != "PUT" && mth != "PATCH" &&
           mth != "DELETE" && mth != "HEAD" && mth != "OPTIONS"))
        throw std::runtime_error("request exceeds limits");
      auto cleanupHttp = [](Impl::Http *x) {
        if (!x)
          return;
        if (x->headers)
          curl_slist_free_all(x->headers);
        if (x->easy)
          curl_easy_cleanup(x->easy);
        delete x;
      };
      std::unique_ptr<Impl::Http, decltype(cleanupHttp)> h(new Impl::Http,
                                                           cleanupHttp);
      h->easy = curl_easy_init();
      h->maxBytes = (size_t)std::clamp(numProp(c, v, "maxBytes", 262144), 1,
                                       (int)BODY_LIMIT);
      h->id = impl_->next++;
      curl_easy_setopt(h->easy, CURLOPT_URL, url.c_str());
      curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(h->easy, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(h->easy, CURLOPT_WRITEFUNCTION, Impl::bodyCb);
      curl_easy_setopt(h->easy, CURLOPT_WRITEDATA, h.get());
      curl_easy_setopt(h->easy, CURLOPT_HEADERFUNCTION, Impl::headCb);
      curl_easy_setopt(h->easy, CURLOPT_HEADERDATA, h.get());
      curl_easy_setopt(h->easy, CURLOPT_PRIVATE, h.get());
      curl_easy_setopt(
          h->easy, CURLOPT_TIMEOUT_MS,
          (long)std::clamp(numProp(c, v, "timeoutMs", 30000), 1, 120000));
      if (mth != "GET")
        curl_easy_setopt(h->easy, CURLOPT_CUSTOMREQUEST, mth.c_str());
      if (mth == "HEAD")
        curl_easy_setopt(h->easy, CURLOPT_NOBODY, 1L);
      if (!body.empty()) {
        curl_easy_setopt(h->easy, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(h->easy, CURLOPT_COPYPOSTFIELDS, body.data());
      }
      ValueGuard hv(c, JS_GetPropertyStr(c, v, "headers"));
      if (JS_IsObject(hv.value)) {
        JSPropertyEnum *tab;
        uint32_t count;
        if (JS_GetOwnPropertyNames(c, &tab, &count, hv.value,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >=
            0) {
          if (count > 64) {
            JS_FreePropertyEnum(c, tab, count);
            throw std::runtime_error("too many headers");
          }
          for (uint32_t i = 0; i < count; i++) {
            ValueGuard kv(c, JS_AtomToString(c, tab[i].atom));
            std::string k = jsstr(c, kv.value);
            ValueGuard x(c, JS_GetProperty(c, hv.value, tab[i].atom));
            std::string val = jsstr(c, x.value);
            if (k.size() > 128 || val.size() > 4096 ||
                k.find_first_of("\r\n:") != std::string::npos ||
                val.find_first_of("\r\n") != std::string::npos) {
              JS_FreePropertyEnum(c, tab, count);
              throw std::runtime_error("invalid header");
            }
            h->headers =
                curl_slist_append(h->headers, (k + ": " + val).c_str());
            if (!h->headers)
              throw std::runtime_error("could not allocate request headers");
          }
          JS_FreePropertyEnum(c, tab, count);
        }
      }
      if (h->headers)
        curl_easy_setopt(h->easy, CURLOPT_HTTPHEADER, h->headers);
      {
        std::lock_guard<std::recursive_mutex> g(impl_->m);
        if (impl_->requests.size() >= 8) {
          throw std::runtime_error("HTTP request limit reached");
        }
        uint64_t requestId = h->id;
        impl_->requests[requestId] = h.get();
        impl_->pendingHttp.push_back(h.get());
        h.release();
        return done(JS_NewInt64(c, (int64_t)requestId));
      }
    }
    return done(fail(c, "unknown method"));
  } catch (const std::exception &e) {
    if (!JS_IsUndefined(v))
      JS_FreeValue(c, v);
    if (!JS_IsUndefined(a))
      JS_FreeValue(c, a);
    return fail(c, e.what());
  }
}
} // namespace cpzero
