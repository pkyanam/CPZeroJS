#include "quickjs.h"
#include "services.hpp"
#include <lvgl.h>
#ifdef CPZERO_FBDEV
#include "fbdev.hpp"
#else
#include <SDL.h>
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(__unix__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static constexpr int W = 320, H = 170;
static constexpr size_t JS_LIMIT = 16U * 1024U * 1024U;
static constexpr uint64_t JS_BUDGET_MS = 50;
static constexpr int HOST_KEY_ENTER = 0x110001, HOST_KEY_TAB = 0x110002,
                     HOST_KEY_ESCAPE = 0x110003;
struct Widget {
  uint32_t id;
  lv_obj_t *obj;
  std::string kind;
  uint32_t parent;
  std::unordered_set<uint32_t> children;
};
struct Host {
  JSRuntime *rt = nullptr;
  JSContext *ctx = nullptr;
#ifndef CPZERO_FBDEV
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
#else
  cpzero::Framebuffer framebuffer;
#endif
  lv_display_t *display = nullptr;
  lv_indev_t *mouse = nullptr;
  lv_indev_t *keyboard = nullptr;
  lv_group_t *group = nullptr;
  std::vector<uint32_t> pixels;
  std::unordered_map<uint32_t, Widget> widgets;
  uint32_t nextId = 1;
  lv_obj_t *screen = nullptr;
  std::string bundle;
  bool watch = false, headless = false, running = true;
  int scale = 3, frames = -1, frame = 0;
  std::string screenshot;
  std::vector<int> keys;
  size_t keyAt = 0;
  uint64_t lastTick = 0;
  Clock::time_point deadline{};
  Clock::time_point appStart = Clock::now();
  bool deadlineActive = false;
  bool injectedRelease = false;
  std::unordered_map<void *, std::string> pendingRejections;
  std::string error;
};
static Host *G = nullptr;
static uint64_t nowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}
static uint32_t lvTick() { return (uint32_t)nowMs(); }
static int interrupt(JSRuntime *, void *opaque) {
  Host *h = (Host *)opaque;
  return h->deadlineActive && Clock::now() > h->deadline;
}
static void setError(Host *h, const std::string &s) {
  if (h->error.empty())
    h->error = s;
}
static bool check(JSContext *ctx, JSValue v, const char *where) {
  if (!JS_IsException(v))
    return true;
  JSValue e = JS_GetException(ctx);
  const char *s = JS_ToCString(ctx, e);
  std::string m = std::string(where) + ": " + (s ? s : "JavaScript exception");
  if (s)
    JS_FreeCString(ctx, s);
  JS_FreeValue(ctx, e);
  setError(G, m);
  std::cerr << "CPZeroJS error: " << m << "\n";
  return false;
}
static bool drainJobs(Host *h, const char *where);
static JSValue run(Host *h, JSValueConst fn, int argc, JSValueConst *argv,
                   const char *where) {
  h->error.clear();
  h->deadline = Clock::now() + std::chrono::milliseconds(JS_BUDGET_MS);
  h->deadlineActive = true;
  JSValue r = JS_Call(h->ctx, fn, JS_UNDEFINED, argc, argv);
  if (!check(h->ctx, r, where)) {
    h->deadlineActive = false;
    JS_FreeValue(h->ctx, r);
    return JS_EXCEPTION;
  }
  JS_FreeValue(h->ctx, r);
  bool ok = drainJobs(h, where);
  h->deadlineActive = false;
  if (!ok)
    return JS_EXCEPTION;
  return JS_UNDEFINED;
}
static void rejectionTracker(JSContext *ctx, JSValueConst promise,
                             JSValueConst reason, bool handled, void *opaque) {
  Host *h = (Host *)opaque;
  void *key = JS_VALUE_GET_PTR(promise);
  if (handled) {
    h->pendingRejections.erase(key);
    return;
  }
  const char *s = JS_ToCString(ctx, reason);
  h->pendingRejections[key] = s ? s : "unknown rejection";
  if (s)
    JS_FreeCString(ctx, s);
}
static bool drainJobs(Host *h, const char *where) {
  JSContext *jobctx = nullptr;
  for (int n = 0; n < 1000; n++) {
    int rc = JS_ExecutePendingJob(h->rt, &jobctx);
    if (rc < 0) {
      JSContext *c = jobctx ? jobctx : h->ctx;
      JSValue e = JS_GetException(c);
      const char *s = JS_ToCString(c, e);
      setError(h, std::string(where) + ": " + (s ? s : "Promise job failed"));
      if (s)
        JS_FreeCString(c, s);
      JS_FreeValue(c, e);
      break;
    }
    if (rc == 0)
      break;
  }
  if (!h->pendingRejections.empty())
    setError(h, "Unhandled promise rejection: " +
                    h->pendingRejections.begin()->second);
  if (!h->error.empty()) {
    std::cerr << "CPZeroJS error: " << h->error << "\n";
    return false;
  }
  return true;
}
static JSValue callGlobal(Host *h, const char *name, int argc,
                          JSValueConst *argv) {
  JSValue global = JS_GetGlobalObject(h->ctx),
          fn = JS_GetPropertyStr(h->ctx, global, name);
  JS_FreeValue(h->ctx, global);
  if (!JS_IsFunction(h->ctx, fn)) {
    JS_FreeValue(h->ctx, fn);
    return JS_UNDEFINED;
  }
  JSValue r = run(h, fn, argc, argv, name);
  JS_FreeValue(h->ctx, fn);
  return r;
}
static std::string str(JSContext *c, JSValueConst v) {
  const char *p = JS_ToCString(c, v);
  if (!p)
    return {};
  std::string s(p);
  JS_FreeCString(c, p);
  return s;
}
static bool number(JSContext *c, JSValueConst v, double *out) {
  return JS_ToFloat64(c, out, v) == 0 && std::isfinite(*out);
}
static JSValue prop(JSContext *c, JSValueConst o, const char *k) {
  return JS_GetPropertyStr(c, o, k);
}
static bool has(JSContext *c, JSValueConst o, const char *k) {
  JSAtom a = JS_NewAtom(c, k);
  int yes = JS_HasProperty(c, o, a);
  JS_FreeAtom(c, a);
  return yes > 0;
}
static int32_t sizeProp(JSContext *c, JSValueConst p, int fallback) {
  if (JS_IsUndefined(p) || JS_IsNull(p))
    return fallback;
  if (JS_IsString(p) && str(c, p) == "100%")
    return LV_PCT(100);
  double d = 0;
  if (!number(c, p, &d))
    return fallback;
  return (int32_t)std::clamp(d, 0.0, 4096.0);
}
static lv_color_t colorProp(JSContext *c, JSValueConst p, lv_color_t fallback) {
  if (!JS_IsString(p))
    return fallback;
  std::string s = str(c, p);
  if (s.size() != 7 || s[0] != '#')
    return fallback;
  char *end = nullptr;
  unsigned long x = strtoul(s.c_str() + 1, &end, 16);
  if (!end || *end)
    return fallback;
  return lv_color_make((x >> 16) & 255, (x >> 8) & 255, x & 255);
}
static lv_obj_t *textTarget(Widget &w) {
  if (w.kind == "button")
    return lv_obj_get_child(w.obj, 0);
  return w.obj;
}
static lv_obj_t *makeObject(const std::string &kind, lv_obj_t *parent) {
  if (kind == "column" || kind == "screen")
    return lv_obj_create(parent);
  if (kind == "row")
    return lv_obj_create(parent);
  if (kind == "label")
    return lv_label_create(parent);
  if (kind == "button") {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(b);
    lv_obj_center(label);
    return b;
  }
  if (kind == "input")
    return lv_textarea_create(parent);
  if (kind == "bar")
    return lv_bar_create(parent);
  if (kind == "box")
    return lv_obj_create(parent);
  return nullptr;
}
static void applyProps(Host *h, Widget &w, JSValueConst p) {
  lv_obj_t *o = w.obj;
  JSContext *c = h->ctx;
  if (has(c, p, "width")) {
    JSValue v = prop(c, p, "width");
    lv_obj_set_width(o, sizeProp(c, v, LV_SIZE_CONTENT));
    JS_FreeValue(c, v);
  }
  if (has(c, p, "height")) {
    JSValue v = prop(c, p, "height");
    lv_obj_set_height(o, sizeProp(c, v, LV_SIZE_CONTENT));
    JS_FreeValue(c, v);
  }
  if (has(c, p, "hidden")) {
    JSValue v = prop(c, p, "hidden");
    if (JS_ToBool(c, v))
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "disabled")) {
    JSValue v = prop(c, p, "disabled");
    if (JS_ToBool(c, v))
      lv_obj_add_state(o, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(o, LV_STATE_DISABLED);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "grow")) {
    JSValue v = prop(c, p, "grow");
    double n = 0;
    if (number(c, v, &n))
      lv_obj_set_flex_grow(o, (uint8_t)std::clamp(n, 0.0, 255.0));
    JS_FreeValue(c, v);
  }
  if (has(c, p, "gap")) {
    JSValue v = prop(c, p, "gap");
    double n;
    if (number(c, v, &n))
      lv_obj_set_style_pad_row(o, (int)n, 0),
          lv_obj_set_style_pad_column(o, (int)n, 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "padding")) {
    JSValue v = prop(c, p, "padding");
    double n;
    if (number(c, v, &n))
      lv_obj_set_style_pad_all(o, (int)n, 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "bg")) {
    JSValue v = prop(c, p, "bg");
    lv_obj_set_style_bg_color(o, colorProp(c, v, lv_color_white()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "color")) {
    JSValue v = prop(c, p, "color");
    lv_obj_set_style_text_color(textTarget(w),
                                colorProp(c, v, lv_color_black()), 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "fontSize")) {
    JSValue v = prop(c, p, "fontSize");
    double n = 14;
    number(c, v, &n);
    const lv_font_t *f = n >= 20   ? &lv_font_montserrat_20
                         : n >= 16 ? &lv_font_montserrat_16
                                   : &lv_font_montserrat_14;
    lv_obj_set_style_text_font(textTarget(w), f, 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "text")) {
    JSValue v = prop(c, p, "text");
    std::string s = str(c, v);
    if (w.kind == "input")
      lv_textarea_set_text(o, s.c_str());
    else if (w.kind == "label" || w.kind == "button")
      lv_label_set_text(textTarget(w), s.c_str());
    JS_FreeValue(c, v);
  }
  if (has(c, p, "value")) {
    JSValue v = prop(c, p, "value");
    std::string s = str(c, v);
    if (w.kind == "input")
      lv_textarea_set_text(o, s.c_str());
    JS_FreeValue(c, v);
  }
  if (w.kind == "column" || w.kind == "screen")
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  else if (w.kind == "row")
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  if (w.kind == "bar") {
    int32_t mn = lv_bar_get_min_value(o), mx = lv_bar_get_max_value(o);
    if (has(c, p, "min")) {
      JSValue q = prop(c, p, "min");
      double n = mn;
      if (number(c, q, &n))
        mn = (int32_t)n;
      JS_FreeValue(c, q);
    }
    if (has(c, p, "max")) {
      JSValue q = prop(c, p, "max");
      double n = mx;
      if (number(c, q, &n))
        mx = (int32_t)n;
      JS_FreeValue(c, q);
    }
    if (has(c, p, "min") || has(c, p, "max"))
      lv_bar_set_range(o, mn, mx);
    if (has(c, p, "value")) {
      JSValue q = prop(c, p, "value");
      double n = lv_bar_get_value(o);
      number(c, q, &n);
      lv_bar_set_value(o, (int32_t)n, LV_ANIM_OFF);
      JS_FreeValue(c, q);
    }
  }
}
static void dispatch(Host *h, uint32_t id, const char *event,
                     const char *value) {
  JSValue a[3] = {JS_NewInt32(h->ctx, (int)id), JS_NewString(h->ctx, event),
                  JS_NewString(h->ctx, value ? value : "")};
  JSValue r = callGlobal(h, "__cpDispatch", 3, a);
  if (JS_IsException(r)) {
    std::cerr << h->error << "\n";
    h->running = false;
  }
  for (auto &v : a)
    JS_FreeValue(h->ctx, v);
  JS_FreeValue(h->ctx, r);
}
static void onLvEvent(lv_event_t *e) {
  auto *h = G;
  // Programmatic setters can synchronously trigger LVGL change events. Do not
  // re-enter JS (or reset its execution deadline) from inside a native binding.
  if (!h->running || h->deadlineActive)
    return;
  uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  auto it = h->widgets.find(id);
  if (it == h->widgets.end())
    return;
  auto &w = it->second;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
    dispatch(h, id, "press", "");
  else if (code == LV_EVENT_VALUE_CHANGED && w.kind == "input")
    dispatch(h, id, "change", lv_textarea_get_text(w.obj));
}
static JSValue cpCreate(JSContext *c, JSValueConst, int argc,
                        JSValueConst *argv) {
  Host *h = G;
  if (argc < 3 || !JS_IsNumber(argv[1]))
    return JS_ThrowTypeError(c, "create(kind,parent,props) expected");
  std::string kind = str(c, argv[0]);
  int32_t parent = 0;
  JS_ToInt32(c, &parent, argv[1]);
  lv_obj_t *par = h->screen;
  if (parent) {
    auto p = h->widgets.find(parent);
    if (p == h->widgets.end())
      return JS_ThrowReferenceError(c, "parent widget not found");
    par = p->second.obj;
  }
  lv_obj_t *obj = makeObject(kind, par);
  if (!obj)
    return JS_ThrowTypeError(c, "unsupported widget kind: %s", kind.c_str());
  uint32_t id = h->nextId++;
  if (h->widgets.size() >= 10000) {
    lv_obj_delete(obj);
    return JS_ThrowRangeError(c, "widget limit (10000) exceeded");
  }
  if (kind == "screen") {
    lv_obj_set_size(obj, W, H);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
  }
  if (kind == "row" || kind == "column" || kind == "box") {
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  }
  Widget w{id, obj, kind, (uint32_t)parent, {}};
  if (parent)
    h->widgets.at(parent).children.insert(id);
  h->widgets.emplace(id, std::move(w));
  applyProps(h, h->widgets.at(id), argv[2]);
  if (kind == "button" || kind == "input") {
    lv_obj_add_event_cb(obj, onLvEvent,
                        kind == "button" ? LV_EVENT_CLICKED
                                         : LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)id);
    lv_group_add_obj(h->group, obj);
    if (!lv_group_get_focused(h->group))
      lv_group_focus_obj(obj);
  }
  return JS_NewInt32(c, (int)id);
}
static JSValue cpUpdate(JSContext *c, JSValueConst, int argc,
                        JSValueConst *argv) {
  Host *h = G;
  int32_t id;
  if (argc < 2 || JS_ToInt32(c, &id, argv[0]) < 0)
    return JS_ThrowTypeError(c, "update(id,props) expected");
  auto it = h->widgets.find(id);
  if (it == h->widgets.end())
    return JS_ThrowReferenceError(c, "widget %d is disposed", id);
  applyProps(h, it->second, argv[1]);
  return JS_UNDEFINED;
}
static void eraseTree(Host *h, uint32_t id) {
  auto it = h->widgets.find(id);
  if (it == h->widgets.end())
    return;
  auto kids = it->second.children;
  for (uint32_t child : kids)
    eraseTree(h, child);
  uint32_t par = it->second.parent;
  if (par) {
    auto p = h->widgets.find(par);
    if (p != h->widgets.end()) {
      p->second.children.erase(id);
    }
  }
  h->widgets.erase(id);
}
static JSValue cpRemove(JSContext *c, JSValueConst, int argc,
                        JSValueConst *argv) {
  int32_t id;
  if (argc < 1 || JS_ToInt32(c, &id, argv[0]) < 0)
    return JS_ThrowTypeError(c, "remove(id) expected");
  auto it = G->widgets.find(id);
  if (it != G->widgets.end()) {
    lv_obj_delete(it->second.obj);
    eraseTree(G, (uint32_t)id);
  }
  return JS_UNDEFINED;
}
static JSValue cpLog(JSContext *c, JSValueConst, int argc, JSValueConst *argv) {
  if (argc) {
    std::cout << str(c, argv[0]) << "\n";
    std::cout.flush();
  }
  return JS_UNDEFINED;
}
static std::string safeName(const std::string &s) {
  std::string n;
  for (unsigned char c : s)
    if (std::isalnum(c) || c == '-' || c == '_')
      n.push_back((char)c);
  if (n.empty())
    n = "app";
  return n.substr(0, 48);
}
static fs::path storageDir() {
  if (const char *p = getenv("CPZERO_DATA_DIR"))
    return fs::path(p);
  const char *home = getenv("HOME");
  return fs::path(home ? home : ".") / ".local" / "share" / "cpzero" /
         safeName(fs::path(G->bundle).stem().string());
}
static JSValue cpInvoke(JSContext *c, JSValueConst, int argc,
                        JSValueConst *argv) {
  if (argc < 3)
    return JS_ThrowTypeError(c, "invoke(service,method,jsonArgs) expected");
  std::string service = str(c, argv[0]), method = str(c, argv[1]),
              json = str(c, argv[2]);
  if (service != "storage") {
    const auto *handler = cpzero::findService(service);
    if (!handler)
      return JS_ThrowTypeError(c, "unknown native service '%s'",
                               service.c_str());
    try {
      std::string result = (*handler)(method, json);
      if (result.size() > 65536)
        return JS_ThrowRangeError(c, "native service result exceeds 64 KiB");
      JSValue validated = JS_ParseJSON(c, result.c_str(), result.size(),
                                       "native service result");
      if (JS_IsException(validated))
        return JS_ThrowTypeError(c, "native service returned invalid JSON");
      JS_FreeValue(c, validated);
      return JS_NewStringLen(c, result.data(), result.size());
    } catch (const std::exception &e) {
      return JS_ThrowInternalError(c, "native service %s: %s", service.c_str(),
                                   e.what());
    }
  }
  JSValue parsed =
      JS_ParseJSON(c, json.c_str(), json.size(), "service arguments");
  if (JS_IsException(parsed))
    return JS_EXCEPTION;
  std::string key;
  JSValue k = JS_GetPropertyUint32(c, parsed, 0);
  key = str(c, k);
  JS_FreeValue(c, k);
  if (key.empty() || key.size() > 128 || key.find("..") != std::string::npos ||
      key.find('/') != std::string::npos ||
      key.find('\\') != std::string::npos) {
    JS_FreeValue(c, parsed);
    return JS_ThrowTypeError(c, "invalid storage key");
  }
  fs::path dir = storageDir(), file = dir / (key + ".json");
  std::string out = "null";
  try {
    fs::create_directories(dir);
    if (method == "get") {
      std::ifstream f(file, std::ios::binary);
      if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        if (out.size() > 65536)
          throw std::runtime_error("stored value exceeds 64 KiB");
      }
    } else if (method == "set") {
      JSValue v = JS_GetPropertyUint32(c, parsed, 1);
      JSValue jsonValue = JS_JSONStringify(c, v, JS_UNDEFINED, JS_UNDEFINED);
      JS_FreeValue(c, v);
      if (JS_IsException(jsonValue) || JS_IsUndefined(jsonValue))
        throw std::runtime_error("storage value is not JSON serializable");
      size_t len = 0;
      const char *p = JS_ToCStringLen(c, &len, jsonValue);
      if (!p) {
        JS_FreeValue(c, jsonValue);
        throw std::runtime_error("storage value serialization failed");
      }
      std::string tmp(p, len);
      JS_FreeCString(c, p);
      JS_FreeValue(c, jsonValue);
      if (tmp.size() > 65536)
        throw std::runtime_error("stored value exceeds 64 KiB");
      fs::path staging = file;
      staging += ".tmp";
      {
        std::ofstream f(staging, std::ios::binary | std::ios::trunc);
        f.write(tmp.data(), tmp.size());
        if (!f)
          throw std::runtime_error("storage write failed");
      }
      fs::rename(staging, file);
      out = "null";
    } else
      throw std::runtime_error("unsupported storage method");
  } catch (const std::exception &e) {
    JS_FreeValue(c, parsed);
    return JS_ThrowInternalError(c, "storage: %s", e.what());
  }
  JS_FreeValue(c, parsed);
  return JS_NewStringLen(c, out.data(), out.size());
}
static JSValue cpStats(JSContext *c, JSValueConst, int, JSValueConst *) {
  JSMemoryUsage mem{};
  JS_ComputeMemoryUsage(G->rt, &mem);
  JSValue o = JS_NewObject(c);
  JS_SetPropertyStr(c, o, "widgets", JS_NewInt32(c, (int)G->widgets.size()));
  JS_SetPropertyStr(c, o, "nextId", JS_NewInt32(c, (int)G->nextId));
  JS_SetPropertyStr(c, o, "jsMemoryBytes",
                    JS_NewInt64(c, (int64_t)mem.memory_used_size));
  JS_SetPropertyStr(c, o, "nativeMemoryCapacityBytes",
                    JS_NewInt64(c, (int64_t)(LV_MEM_SIZE)));
  return o;
}
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *p) {
  Host *h = G;
  int32_t x1 = std::max(a->x1, 0), y1 = std::max(a->y1, 0),
          x2 = std::min(a->x2, W - 1), y2 = std::min(a->y2, H - 1);
  for (int y = y1; y <= y2; y++) {
    auto *src = (uint32_t *)p + (size_t)(y - a->y1) * (a->x2 - a->x1 + 1) +
                (x1 - a->x1);
    std::copy(src, src + (x2 - x1 + 1), h->pixels.begin() + (size_t)y * W + x1);
  }
#ifdef CPZERO_FBDEV
  h->framebuffer.present(h->pixels.data());
#else
  if (h->texture) {
    SDL_UpdateTexture(h->texture, nullptr, h->pixels.data(),
                      W * sizeof(uint32_t));
    SDL_RenderClear(h->renderer);
    SDL_RenderCopy(h->renderer, h->texture, nullptr, nullptr);
    SDL_RenderPresent(h->renderer);
  }
#endif
  lv_display_flush_ready(d);
}
#ifndef CPZERO_FBDEV
static void pointerRead(lv_indev_t *, lv_indev_data_t *d) {
  int x = 0, y = 0;
  uint32_t b = SDL_GetMouseState(&x, &y);
  d->point.x = x / G->scale;
  d->point.y = y / G->scale;
  d->state =
      (b & SDL_BUTTON_LMASK) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static uint32_t mapKey(SDL_Keycode k) {
  switch (k) {
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return LV_KEY_ENTER;
  case SDLK_TAB:
    return LV_KEY_NEXT;
  case SDLK_BACKSPACE:
    return LV_KEY_BACKSPACE;
  case SDLK_ESCAPE:
    return LV_KEY_ESC;
  case SDLK_LEFT:
    return LV_KEY_LEFT;
  case SDLK_RIGHT:
    return LV_KEY_RIGHT;
  case SDLK_UP:
    return LV_KEY_UP;
  case SDLK_DOWN:
    return LV_KEY_DOWN;
  default:
    return (k >= 32 && k < 127) ? (uint32_t)k : 0;
  }
}
#endif
#ifdef CPZERO_FBDEV
static void keyRead(lv_indev_t *, lv_indev_data_t *d) {
  static uint32_t key = 0;
  static bool pressed = false;
  uint32_t nextKey = 0;
  bool nextPressed = false;
  bool readMore = G->framebuffer.readKey(nextKey, nextPressed);
  if (readMore) {
    key = nextKey;
    pressed = nextPressed;
  }
  d->key = key;
  d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  d->continue_reading = readMore;
}
#else
static uint32_t mapHostKey(int k) {
  switch (k) {
  case HOST_KEY_ENTER:
    return LV_KEY_ENTER;
  case HOST_KEY_TAB:
    return LV_KEY_NEXT;
  case HOST_KEY_ESCAPE:
    return LV_KEY_ESC;
  default:
    return k >= 32 && k < 127 ? (uint32_t)k : 0;
  }
}
static void keyRead(lv_indev_t *, lv_indev_data_t *d) {
  static uint32_t held = 0;
  static bool heldPressed = false;
  d->continue_reading = false;
  if (G->keyAt < G->keys.size()) {
    held = mapHostKey(G->keys[G->keyAt]);
    d->state =
        G->injectedRelease ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
    d->key = held;
    if (G->injectedRelease) {
      G->injectedRelease = false;
      G->keyAt++;
    } else
      G->injectedRelease = true;
    d->continue_reading = G->keyAt < G->keys.size() || G->injectedRelease;
    heldPressed = d->state == LV_INDEV_STATE_PRESSED;
    return;
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      G->running = false;
      continue;
    }
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
      uint32_t key = mapKey(e.key.keysym.sym);
      if (!key)
        continue;
      held = key;
      d->key = key;
      d->state = e.type == SDL_KEYDOWN ? LV_INDEV_STATE_PRESSED
                                       : LV_INDEV_STATE_RELEASED;
      heldPressed = d->state == LV_INDEV_STATE_PRESSED;
      d->continue_reading = false;
      return;
    }
  }
  d->key = held;
  d->state = heldPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif
static bool screenshotWrite(Host *h, const std::string &path) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    return false;
  f << "P6\n" << W << " " << H << "\n255\n";
  for (uint32_t p : h->pixels) {
    char rgb[3] = {(char)((p >> 16) & 255), (char)((p >> 8) & 255),
                   (char)(p & 255)};
    f.write(rgb, 3);
  }
  return bool(f);
}
static void installBridge(Host *h) {
  JSValue global = JS_GetGlobalObject(h->ctx), cp = JS_NewObject(h->ctx);
  JS_SetPropertyStr(h->ctx, cp, "create",
                    JS_NewCFunction(h->ctx, cpCreate, "create", 3));
  JS_SetPropertyStr(h->ctx, cp, "update",
                    JS_NewCFunction(h->ctx, cpUpdate, "update", 2));
  JS_SetPropertyStr(h->ctx, cp, "remove",
                    JS_NewCFunction(h->ctx, cpRemove, "remove", 1));
  JS_SetPropertyStr(h->ctx, cp, "log",
                    JS_NewCFunction(h->ctx, cpLog, "log", 1));
  JS_SetPropertyStr(h->ctx, cp, "invoke",
                    JS_NewCFunction(h->ctx, cpInvoke, "invoke", 3));
  JS_SetPropertyStr(h->ctx, cp, "stats",
                    JS_NewCFunction(h->ctx, cpStats, "stats", 0));
  JS_SetPropertyStr(h->ctx, global, "__cp", cp);
  JS_FreeValue(h->ctx, global);
}
static void printException(Host *h) {
  JSValue e = JS_GetException(h->ctx);
  const char *s = JS_ToCString(h->ctx, e);
  std::cerr << "CPZeroJS error: " << (s ? s : "unknown exception") << "\n";
  if (s)
    JS_FreeCString(h->ctx, s);
  JS_FreeValue(h->ctx, e);
}
static bool loadBundle(Host *h) {
  std::ifstream f(h->bundle, std::ios::binary);
  if (!f) {
    std::cerr << "Cannot read bundle: " << h->bundle << "\n";
    return false;
  }
  std::string src((std::istreambuf_iterator<char>(f)), {});
  if (h->ctx)
    JS_FreeContext(h->ctx);
  if (h->rt)
    JS_FreeRuntime(h->rt);
  h->rt = JS_NewRuntime();
  if (!h->rt)
    return false;
  JS_SetMemoryLimit(h->rt, JS_LIMIT);
  JS_SetMaxStackSize(h->rt, 1024 * 1024);
  JS_SetInterruptHandler(h->rt, interrupt, h);
  JS_SetHostPromiseRejectionTracker(h->rt, rejectionTracker, h);
  h->ctx = JS_NewContext(h->rt);
  if (!h->ctx)
    return false;
  installBridge(h);
  h->pendingRejections.clear();
  h->appStart = Clock::now();
  h->deadline = Clock::now() + std::chrono::milliseconds(JS_BUDGET_MS);
  h->deadlineActive = true;
  JSValue v = JS_Eval(h->ctx, src.c_str(), src.size(), h->bundle.c_str(),
                      JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v)) {
    h->deadlineActive = false;
    printException(h);
    JS_FreeValue(h->ctx, v);
    return false;
  }
  JS_FreeValue(h->ctx, v);
  bool jobsOk = drainJobs(h, "startup promise job");
  h->deadlineActive = false;
  return jobsOk;
}
static bool setupLvgl(Host *h) {
  lv_init();
  lv_tick_set_cb(lvTick);
  h->pixels.assign((size_t)W * H, 0xfff7f8fa);
  h->display = lv_display_create(W, H);
  if (!h->display)
    return false;
  auto *buf = (uint32_t *)std::malloc(W * 40 * sizeof(uint32_t));
  if (!buf)
    return false;
  lv_display_set_buffers(h->display, buf, nullptr, W * 40 * sizeof(uint32_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(h->display, flush);
  h->screen = lv_display_get_screen_active(h->display);
  lv_obj_set_style_bg_color(h->screen, lv_color_hex(0xf7f8fa), 0);
  lv_obj_set_style_bg_opa(h->screen, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(h->screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(h->screen, 0, 0);
  lv_obj_set_style_border_width(h->screen, 0, 0);
  lv_obj_set_style_radius(h->screen, 0, 0);
  lv_obj_set_scrollbar_mode(h->screen, LV_SCROLLBAR_MODE_OFF);
  h->group = lv_group_create();
  lv_group_set_default(h->group);
#ifndef CPZERO_FBDEV
  h->mouse = lv_indev_create();
  lv_indev_set_type(h->mouse, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(h->mouse, pointerRead);
#endif
  h->keyboard = lv_indev_create();
  lv_indev_set_type(h->keyboard, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(h->keyboard, keyRead);
  lv_indev_set_group(h->keyboard, h->group);
  return true;
}
#ifndef CPZERO_FBDEV
static bool setupSdl(Host *h) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL: " << SDL_GetError() << "\n";
    return false;
  }
  if (!h->headless)
    h->window = SDL_CreateWindow("CPZeroJS", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, W * h->scale,
                                 H * h->scale, SDL_WINDOW_SHOWN);
  if (h->window) {
    h->renderer = SDL_CreateRenderer(
        h->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!h->renderer)
      h->renderer = SDL_CreateRenderer(h->window, -1, SDL_RENDERER_SOFTWARE);
    if (!h->renderer)
      return false;
    SDL_RenderSetLogicalSize(h->renderer, W, H);
    h->texture = SDL_CreateTexture(h->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, W, H);
  }
  return true;
}
#else
static bool setupPlatform(Host *h) {
  try {
    h->framebuffer.openDevices(std::getenv("CPZERO_FRAMEBUFFER"),
                               std::getenv("CPZERO_INPUT_DEVICE"));
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Framebuffer: " << e.what() << "\n";
    return false;
  }
}
#endif

static void teardownUI(Host *h) {
  for (auto &kv : h->widgets) {
    if (kv.second.parent == 0)
      lv_obj_delete(kv.second.obj);
  }
  h->widgets.clear();
}
static void usage() {
  std::cerr
      << "usage: cpzero-host <bundle.js> [--watch] [--scale N] [--headless] "
         "[--frames N] [--screenshot path.ppm] [--keys Enter,Tab,...]\n";
}
int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  Host h;
  G = &h;
  h.bundle = argv[1];
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--watch")
      h.watch = true;
    else if (a == "--headless")
      h.headless = true;
    else if (a == "--scale" && i + 1 < argc)
      h.scale = std::clamp(atoi(argv[++i]), 1, 8);
    else if (a == "--frames" && i + 1 < argc)
      h.frames = std::max(1, atoi(argv[++i]));
    else if (a == "--screenshot" && i + 1 < argc)
      h.screenshot = argv[++i];
    else if (a == "--keys" && i + 1 < argc) {
      std::stringstream ss(argv[++i]);
      std::string k;
      while (std::getline(ss, k, ',')) {
        if (k == "Enter")
          h.keys.push_back(HOST_KEY_ENTER);
        else if (k == "Tab")
          h.keys.push_back(HOST_KEY_TAB);
        else if (k == "Escape")
          h.keys.push_back(HOST_KEY_ESCAPE);
        else if (k.size() == 1)
          h.keys.push_back((int)k[0]);
      }
    } else {
      usage();
      return 2;
    }
  }
#ifdef CPZERO_FBDEV
  if (!setupPlatform(&h))
    return 1;
#else
  if (!setupSdl(&h)) {
    SDL_Quit();
    return 1;
  }
#endif
  if (!setupLvgl(&h)) {
    return 1;
  }
  h.rt = JS_NewRuntime();
  if (!h.rt) {
    return 1;
  }
  JS_SetMemoryLimit(h.rt, JS_LIMIT);
  JS_SetMaxStackSize(h.rt, 1024 * 1024);
  JS_SetInterruptHandler(h.rt, interrupt, &h);
  if (!loadBundle(&h)) {
    JS_FreeContext(h.ctx);
    JS_FreeRuntime(h.rt);
    return 1;
  }
  auto stamp = fs::last_write_time(h.bundle);
  auto reloadAt = Clock::now();
  while (h.running && (h.frames < 0 || h.frame < h.frames)) {
    auto start = Clock::now();
#ifndef CPZERO_FBDEV
    SDL_PumpEvents();
#endif
    uint64_t t =
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - h.appStart)
            .count();
    JSValue arg = JS_NewInt64(h.ctx, (int64_t)t);
    JSValue tick = callGlobal(&h, "__cpTick", 1, &arg);
    JS_FreeValue(h.ctx, arg);
    if (JS_IsException(tick)) {
      h.running = false;
      break;
    }
    JS_FreeValue(h.ctx, tick);
    if (h.keyAt < h.keys.size())
      lv_indev_read(h.keyboard);
    lv_timer_handler();
    if (!h.running)
      break;
    if (!h.screenshot.empty() && h.frame == 0) {
      if (!screenshotWrite(&h, h.screenshot)) {
        std::cerr << "Could not write screenshot: " << h.screenshot << "\n";
        h.running = false;
      }
    }
    h.frame++;
    if (h.watch && Clock::now() - reloadAt > std::chrono::milliseconds(200)) {
      reloadAt = Clock::now();
      std::error_code ec;
      auto next = fs::last_write_time(h.bundle, ec);
      if (!ec && next != stamp) {
        stamp = next;
        teardownUI(&h);
        if (!loadBundle(&h)) {
          std::cerr << "Reload failed; host stopped.\n";
          h.running = false;
        } else
          std::cerr << "Reloaded " << h.bundle << "\n";
      }
    }
#ifdef CPZERO_FBDEV
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
#else
    if (h.headless)
      SDL_Delay(1);
    else {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - start)
                         .count();
      if (elapsed < 16)
        SDL_Delay((uint32_t)(16 - elapsed));
    }
#endif
  }
  if (h.screenshot.size() && h.frame > 0 && h.running)
    screenshotWrite(&h, h.screenshot);
  if (getenv("CPZERO_DEBUG_LAYOUT")) {
    lv_obj_update_layout(h.screen);
    for (const auto &entry : h.widgets) {
      auto *object = entry.second.obj;
      std::cerr << entry.first << " " << entry.second.kind << " x=" << lv_obj_get_x(object)
                << " y=" << lv_obj_get_y(object) << " w=" << lv_obj_get_width(object)
                << " h=" << lv_obj_get_height(object) << "\n";
    }
  }
  teardownUI(&h);
  if (h.ctx)
    JS_FreeContext(h.ctx);
  if (h.rt)
    JS_FreeRuntime(h.rt);
  if (h.display)
    lv_display_delete(h.display);
  if (h.group)
    lv_group_delete(h.group);
#ifndef CPZERO_FBDEV
  if (h.texture)
    SDL_DestroyTexture(h.texture);
  if (h.renderer)
    SDL_DestroyRenderer(h.renderer);
  if (h.window)
    SDL_DestroyWindow(h.window);
  SDL_Quit();
#endif
  return h.error.empty() ? 0 : 1;
}
