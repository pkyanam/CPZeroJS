#include "quickjs.h"
#include "services.hpp"
#include "async_services.hpp"
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
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
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
                     HOST_KEY_ESCAPE = 0x110003, HOST_KEY_SHIFTTAB = 0x110004,
                     HOST_KEY_UP = 0x110005, HOST_KEY_DOWN = 0x110006,
                     HOST_KEY_LEFT = 0x110007, HOST_KEY_RIGHT = 0x110008,
                     HOST_KEY_PAGEUP = 0x110009, HOST_KEY_PAGEDOWN = 0x11000a,
                     HOST_KEY_HOME = 0x11000b, HOST_KEY_END = 0x11000c,
                     HOST_KEY_BACKSPACE = 0x11000d;
struct Widget {
  uint32_t id;
  lv_obj_t *obj;
  std::string kind;
  uint32_t parent;
  std::unordered_set<uint32_t> children;
  bool focusable = false;
  bool explicitColor = false, explicitFont = false;
};
#ifndef CPZERO_FBDEV
struct PointerSample { int x=0,y=0; bool pressed=false,edge=false; };
#endif
struct FocusScope { uint32_t container=0, previousFocus=0; lv_group_t *previousGroup=nullptr,*group=nullptr; std::vector<uint32_t> members; };
struct Host {
  JSRuntime *rt = nullptr;
  JSContext *ctx = nullptr;
  std::unique_ptr<cpzero::AsyncServices> async;
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
  std::vector<FocusScope> focusScopes;
  std::vector<uint32_t> pixels;
  std::unordered_map<uint32_t, Widget> widgets;
  uint32_t nextId = 1;
  lv_obj_t *screen = nullptr;
  std::string bundle;
  bool watch = false, headless = false, running = true;
  int scale = 3, frames = -1, frame = 0;
  std::string screenshot;
  std::vector<int> keys;
  uint32_t heldKey = 0;
  bool heldKeyPressed = false;
  std::string injectText;
  bool textInjected = false;
  std::string suppressedText;
  uint32_t suppressTextUntil = 0;
  bool screenshotRequested = false;
#ifndef CPZERO_FBDEV
  std::deque<SDL_Event> keyEvents;
  std::deque<PointerSample> pointerEvents;
  std::deque<SDL_Event> syntheticEvents;
  int pointerX=0,pointerY=0;
  bool pointerPressed=false;
#endif
  int clickX = -1, clickY = -1;
  bool clickInjected = false;
  bool keysBatch = false;
#ifndef CPZERO_FBDEV
  size_t keysInjected = 0;
  bool keyReleasePending = false;
  SDL_Keycode injectedKey = SDLK_UNKNOWN;
  SDL_Keymod injectedMods = KMOD_NONE;
#endif
  uint64_t lastTick = 0;
  Clock::time_point deadline{};
  Clock::time_point appStart = Clock::now();
  bool deadlineActive = false;
  bool injectedRelease = false;
  std::unordered_map<void *, std::string> pendingRejections;
  std::string error;
};
static Host *G = nullptr;
LV_FONT_DECLARE(lv_font_cpzero_punct_8);
LV_FONT_DECLARE(lv_font_cpzero_punct_10);
LV_FONT_DECLARE(lv_font_cpzero_punct_12);
LV_FONT_DECLARE(lv_font_cpzero_punct_14);
LV_FONT_DECLARE(lv_font_cpzero_punct_16);
LV_FONT_DECLARE(lv_font_cpzero_punct_20);
static const lv_font_t *fontForSize(double n) {
  if(n>=20) return &lv_font_cpzero_punct_20;
  if(n>=16) return &lv_font_cpzero_punct_16;
  if(n>=14) return &lv_font_cpzero_punct_14;
  if(n>=12) return &lv_font_cpzero_punct_12;
  if(n>=10) return &lv_font_cpzero_punct_10;
  return &lv_font_cpzero_punct_8;
}
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
  if (JS_IsString(p)) {
    std::string s=str(c,p);
    if(s=="content") return LV_SIZE_CONTENT;
    if(!s.empty()&&s.back()=='%') { char *end=nullptr; double n=std::strtod(s.c_str(),&end); if(end==s.c_str()+s.size()-1) return LV_PCT((int)std::clamp(n,0.0,100.0)); }
  }
  double d = 0;
  if (!number(c, p, &d))
    return fallback;
  return (int32_t)std::clamp(d, 0.0, 4096.0);
}
static lv_flex_align_t alignProp(JSContext *c, JSValueConst p, lv_flex_align_t fallback) {
  std::string s = str(c, p);
  if (s == "center") return LV_FLEX_ALIGN_CENTER;
  if (s == "end") return LV_FLEX_ALIGN_END;
  if (s == "between") return LV_FLEX_ALIGN_SPACE_BETWEEN;
  if (s == "start") return LV_FLEX_ALIGN_START;
  return fallback;
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
  if (kind == "richText") {
    lv_obj_t *group=lv_spangroup_create(parent);
    lv_spangroup_set_mode(group,LV_SPAN_MODE_BREAK);
    lv_spangroup_set_overflow(group,LV_SPAN_OVERFLOW_CLIP);
    lv_obj_set_width(group,LV_PCT(100));
    return group;
  }
  if (kind == "bar")
    return lv_bar_create(parent);
  if (kind == "box")
    return lv_obj_create(parent);
  return nullptr;
}
static void inheritTextStyles(Host *h,Widget &parent) {
  for(uint32_t childId:parent.children) {
    auto child=h->widgets.find(childId); if(child==h->widgets.end()) continue;
    if(!child->second.explicitColor) lv_obj_set_style_text_color(textTarget(child->second),lv_obj_get_style_text_color(textTarget(parent),0),0);
    if(!child->second.explicitFont) lv_obj_set_style_text_font(textTarget(child->second),lv_obj_get_style_text_font(textTarget(parent),0),0);
    inheritTextStyles(h,child->second);
  }
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
  const char *dims[] = {"minWidth", "maxWidth", "minHeight", "maxHeight"};
  for (int i = 0; i < 4; ++i) if (has(c, p, dims[i])) {
    JSValue v = prop(c, p, dims[i]); int32_t n = sizeProp(c, v, 0);
    if (i == 0) lv_obj_set_style_min_width(o, n, 0);
    if (i == 1) lv_obj_set_style_max_width(o, n, 0);
    if (i == 2) lv_obj_set_style_min_height(o, n, 0);
    if (i == 3) lv_obj_set_style_max_height(o, n, 0);
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
  const char *pads[] = {"paddingX", "paddingY"};
  for (int i = 0; i < 2; ++i) if (has(c, p, pads[i])) {
    JSValue v = prop(c, p, pads[i]); double n;
    if (number(c, v, &n)) {
      if (i == 0) { lv_obj_set_style_pad_left(o, (int)n, 0); lv_obj_set_style_pad_right(o, (int)n, 0); }
      else { lv_obj_set_style_pad_top(o, (int)n, 0); lv_obj_set_style_pad_bottom(o, (int)n, 0); }
    }
    JS_FreeValue(c, v);
  }
  if (has(c, p, "bg")) {
    JSValue v = prop(c, p, "bg");
    lv_obj_set_style_bg_color(o, colorProp(c, v, lv_color_white()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "color")) {
    w.explicitColor = true;
    JSValue v = prop(c, p, "color");
    lv_obj_set_style_text_color(textTarget(w),
                                colorProp(c, v, lv_color_black()), 0);
    JS_FreeValue(c, v);
  }
  if (has(c, p, "radius")) { JSValue v=prop(c,p,"radius"); double n; if(number(c,v,&n)) lv_obj_set_style_radius(o,(int)n,0); JS_FreeValue(c,v); }
  if (has(c, p, "borderWidth")) { JSValue v=prop(c,p,"borderWidth"); double n; if(number(c,v,&n)) lv_obj_set_style_border_width(o,(int)n,0); JS_FreeValue(c,v); }
  if (has(c, p, "borderColor")) { JSValue v=prop(c,p,"borderColor"); lv_obj_set_style_border_color(o,colorProp(c,v,lv_color_black()),0); JS_FreeValue(c,v); }
  if (has(c, p, "opacity")) { JSValue v=prop(c,p,"opacity"); double n; if(number(c,v,&n)) lv_obj_set_style_opa(o,(lv_opa_t)std::clamp(n,0.0,255.0),0); JS_FreeValue(c,v); }
  if (has(c, p, "textAlign")) { JSValue v=prop(c,p,"textAlign"); std::string s=str(c,v); lv_obj_set_style_text_align(textTarget(w),s=="center"?LV_TEXT_ALIGN_CENTER:s=="right"?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT,0); JS_FreeValue(c,v); }
  if (has(c, p, "lineSpacing")) { JSValue v=prop(c,p,"lineSpacing"); double n; if(number(c,v,&n)) lv_obj_set_style_text_line_space(textTarget(w),(int)n,0); JS_FreeValue(c,v); }
  if (has(c, p, "fontSize")) {
    w.explicitFont = true;
    JSValue v = prop(c, p, "fontSize");
    double n = 14;
    number(c, v, &n);
    const lv_font_t *f = fontForSize(n);
    lv_obj_set_style_text_font(textTarget(w), f, 0);
    JS_FreeValue(c, v);
  }
  if(has(c,p,"focusColor")) { JSValue v=prop(c,p,"focusColor"); auto color=colorProp(c,v,lv_color_hex(0x8a8a8a)); lv_obj_set_style_outline_color(o,color,LV_STATE_FOCUSED); lv_obj_set_style_outline_color(o,color,LV_STATE_FOCUS_KEY); JS_FreeValue(c,v); }
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
  if(w.kind=="richText" && has(c,p,"spans")) {
    JSValue list=prop(c,p,"spans"), lenV=prop(c,list,"length"); uint32_t len=0; JS_ToUint32(c,&len,lenV); JS_FreeValue(c,lenV);
    while(lv_spangroup_get_span_count(o)) lv_spangroup_delete_span(o,lv_spangroup_get_child(o,-1));
    for(uint32_t i=0;i<std::min<uint32_t>(len,256);++i) {
      JSValue item=JS_GetPropertyUint32(c,list,i), text=prop(c,item,"text");
      lv_span_t *span=lv_spangroup_new_span(o); lv_span_set_text(span,str(c,text).c_str()); JS_FreeValue(c,text);
      lv_style_t *style=lv_span_get_style(span);
      if(has(c,item,"color")) { JSValue v=prop(c,item,"color"); lv_style_set_text_color(style,colorProp(c,v,lv_color_black())); JS_FreeValue(c,v); }
      if(has(c,item,"fontSize")) { JSValue v=prop(c,item,"fontSize"); double n=12; number(c,v,&n); lv_style_set_text_font(style,fontForSize(n)); JS_FreeValue(c,v); }
      JS_FreeValue(c,item);
    }
    JS_FreeValue(c,list);
    if(has(c,p,"textAlign")) { JSValue v=prop(c,p,"textAlign"); std::string s=str(c,v); lv_spangroup_set_align(o,s=="center"?LV_TEXT_ALIGN_CENTER:s=="right"?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT); JS_FreeValue(c,v); }
  }
  if (w.kind == "label") {
    if (has(c,p,"overflow")) { JSValue v=prop(c,p,"overflow"); std::string s=str(c,v); lv_label_set_long_mode(o,s=="ellipsis"?LV_LABEL_LONG_DOT:s=="clip"?LV_LABEL_LONG_CLIP:LV_LABEL_LONG_WRAP); JS_FreeValue(c,v); }
  }
  if (w.kind == "input") {
    { JSValue v=prop(c,p,"multiline"); lv_textarea_set_one_line(o,!JS_ToBool(c,v)); JS_FreeValue(c,v); }
    if (has(c,p,"maxLength")) { JSValue v=prop(c,p,"maxLength"); int32_t n=0; if(JS_ToInt32(c,&n,v)==0) lv_textarea_set_max_length(o,(uint32_t)std::max(0,n)); JS_FreeValue(c,v); }
    if (has(c,p,"placeholder")) { JSValue v=prop(c,p,"placeholder"); std::string s=str(c,v); lv_textarea_set_placeholder_text(o,s.c_str()); JS_FreeValue(c,v); }
  }
  if (has(c,p,"scroll")) { JSValue v=prop(c,p,"scroll"); std::string s=str(c,v); lv_obj_set_scroll_dir(o,s=="none"?LV_DIR_NONE:s=="vertical"?LV_DIR_VER:s=="horizontal"?LV_DIR_HOR:LV_DIR_ALL); JS_FreeValue(c,v); }
  if (has(c,p,"focusable")) { JSValue v=prop(c,p,"focusable"); if(JS_ToBool(c,v)) { lv_obj_add_flag(o,LV_OBJ_FLAG_CLICK_FOCUSABLE); } else if(w.kind!="button"&&w.kind!="input") lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICK_FOCUSABLE); JS_FreeValue(c,v); }
  if (w.kind == "column" || w.kind == "screen")
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  else if (w.kind == "row")
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  if (w.kind == "column" || w.kind == "row" || w.kind == "screen") {
    lv_flex_align_t main=LV_FLEX_ALIGN_START,cross=LV_FLEX_ALIGN_START;
    if (has(c,p,"align")) { JSValue v=prop(c,p,"align"); cross=alignProp(c,v,LV_FLEX_ALIGN_START); JS_FreeValue(c,v); }
    if (has(c,p,"justify")) { JSValue v=prop(c,p,"justify"); main=alignProp(c,v,LV_FLEX_ALIGN_START); JS_FreeValue(c,v); }
    lv_obj_set_flex_align(o,main,cross,LV_FLEX_ALIGN_START);
  }
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
  inheritTextStyles(h,w);
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
  else if (code == LV_EVENT_READY && w.kind == "input")
    dispatch(h, id, "submit", lv_textarea_get_text(w.obj));
  else if (code == LV_EVENT_FOCUSED) dispatch(h,id,"focus","");
  else if (code == LV_EVENT_DEFOCUSED) dispatch(h,id,"blur","");
  else if (code == LV_EVENT_SCROLL) dispatch(h,id,"scroll","");
}
static bool validSpanProps(JSContext *c, JSValueConst p) {
  if(!has(c,p,"spans")) return true;
  JSValue list=prop(c,p,"spans");
  if(!JS_IsArray(list)) { JS_FreeValue(c,list); JS_ThrowTypeError(c,"richText spans must be an array"); return false; }
  JSValue lenV=prop(c,list,"length"); uint32_t len=0; JS_ToUint32(c,&len,lenV);
  JS_FreeValue(c,lenV); JS_FreeValue(c,list);
  if(len>256) { JS_ThrowRangeError(c,"richText supports at most 256 spans"); return false; }
  return true;
}
static bool insideWidgetTree(Host *h,uint32_t id,uint32_t ancestor) {
  for(uint32_t at=id;at;){ if(at==ancestor) return true; auto it=h->widgets.find(at); if(it==h->widgets.end()) break; at=it->second.parent; }
  return ancestor==0;
}
static bool visibleWidget(Host *h,uint32_t id) {
  for(uint32_t at=id;at;){ auto it=h->widgets.find(at); if(it==h->widgets.end()) return false; if(lv_obj_has_flag(it->second.obj,LV_OBJ_FLAG_HIDDEN)) return false; at=it->second.parent; }
  return true;
}
static lv_group_t *groupForWidget(Host *h,uint32_t id) {
  for(auto it=h->focusScopes.rbegin();it!=h->focusScopes.rend();++it) if(insideWidgetTree(h,id,it->container)) return it->group;
  return h->focusScopes.empty()?h->group:h->focusScopes.front().previousGroup;
}
static bool beginFocusScope(Host *h,uint32_t container,uint32_t initial) {
  auto root=h->widgets.find(container); if(root==h->widgets.end()) return false;
  if(h->focusScopes.size()>=16) return false;
  FocusScope scope; scope.container=container; scope.previousGroup=h->group;
  lv_obj_t *focused=lv_group_get_focused(h->group);
  for(const auto &entry:h->widgets) if(focused==entry.second.obj) { scope.previousFocus=entry.first; break; }
  for(auto &entry:h->widgets) if(entry.second.focusable&&visibleWidget(h,entry.first)&&insideWidgetTree(h,entry.first,container)) {
    scope.members.push_back(entry.first);
  }
  std::sort(scope.members.begin(),scope.members.end());
  if(scope.members.empty()) return false;
  bool initialFound=false; for(uint32_t member:scope.members) if(member==initial) initialFound=true;
  if(initial && !initialFound) return false;
  scope.group=lv_group_create(); if(!scope.group) return false;
  for(uint32_t member:scope.members) { auto &obj=h->widgets.at(member).obj; if(lv_obj_get_group(obj)) lv_group_remove_obj(obj); lv_group_add_obj(scope.group,obj); }
  h->focusScopes.push_back(std::move(scope)); h->group=h->focusScopes.back().group;
  lv_group_set_default(h->group); lv_indev_set_group(h->keyboard,h->group);
  uint32_t focusId=initial?initial:h->focusScopes.back().members.front();
  auto fit=h->widgets.find(focusId); if(fit!=h->widgets.end()) lv_group_focus_obj(fit->second.obj);
  return true;
}
static bool endFocusScope(Host *h,uint32_t container) {
  if(h->focusScopes.empty() || (container && h->focusScopes.back().container!=container)) return false;
  FocusScope scope=std::move(h->focusScopes.back()); h->focusScopes.pop_back();
  for(uint32_t member:scope.members) { auto it=h->widgets.find(member); if(it==h->widgets.end()) continue; if(lv_obj_get_group(it->second.obj)) lv_group_remove_obj(it->second.obj); lv_group_add_obj(scope.previousGroup,it->second.obj); }
  h->group=scope.previousGroup; lv_group_set_default(h->group); lv_indev_set_group(h->keyboard,h->group);
  auto focus=h->widgets.find(scope.previousFocus); if(focus!=h->widgets.end()&&visibleWidget(h,scope.previousFocus)) lv_group_focus_obj(focus->second.obj);
  lv_group_delete(scope.group);
  return true;
}
static JSValue cpCreate(JSContext *c, JSValueConst, int argc,
                        JSValueConst *argv) {
  Host *h = G;
  if (argc < 3 || !JS_IsNumber(argv[1]))
    return JS_ThrowTypeError(c, "create(kind,parent,props) expected");
  std::string kind = str(c, argv[0]);
  if(kind=="richText"&&!validSpanProps(c,argv[2])) return JS_EXCEPTION;
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
  lv_obj_set_scroll_dir(obj,LV_DIR_NONE);
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
    lv_obj_set_style_pad_all(obj,0,0);
  }
  if(kind=="button") { lv_obj_set_style_pad_top(obj,3,0); lv_obj_set_style_pad_bottom(obj,3,0); lv_obj_set_style_pad_left(obj,6,0); lv_obj_set_style_pad_right(obj,6,0); lv_obj_set_style_radius(obj,3,0); }
  if(kind=="input") { lv_obj_set_style_pad_top(obj,3,0); lv_obj_set_style_pad_bottom(obj,3,0); lv_obj_set_style_pad_left(obj,4,0); lv_obj_set_style_pad_right(obj,4,0); }
  if(kind=="button"||kind=="input") { for(lv_state_t state:{(lv_state_t)LV_STATE_FOCUSED,(lv_state_t)LV_STATE_FOCUS_KEY}) { lv_obj_set_style_outline_width(obj,1,state); lv_obj_set_style_outline_color(obj,lv_color_hex(0x8a8a8a),state); lv_obj_set_style_outline_pad(obj,-2,state); } }
  Widget w{id, obj, kind, (uint32_t)parent, {}};
  lv_obj_add_event_cb(obj,onLvEvent,LV_EVENT_SCROLL,(void *)(uintptr_t)id);
  if (parent)
    h->widgets.at(parent).children.insert(id);
  h->widgets.emplace(id, std::move(w));
  if(parent) {
    auto &child=h->widgets.at(id); auto &pw=h->widgets.at(parent);
    lv_obj_set_style_text_color(textTarget(child),lv_obj_get_style_text_color(textTarget(pw),0),0);
    lv_obj_set_style_text_font(textTarget(child),lv_obj_get_style_text_font(textTarget(pw),0),0);
  }
  applyProps(h, h->widgets.at(id), argv[2]);
  bool focusable=kind=="button"||kind=="input";
  if(has(h->ctx,argv[2],"focusable")) { JSValue v=prop(h->ctx,argv[2],"focusable"); focusable=JS_ToBool(h->ctx,v); JS_FreeValue(h->ctx,v); }
  if (kind == "button" || kind == "input") {
    lv_obj_add_event_cb(obj, onLvEvent, kind == "button" ? LV_EVENT_CLICKED : LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)id);
    if(kind=="input") lv_obj_add_event_cb(obj,onLvEvent,LV_EVENT_READY,(void *)(uintptr_t)id);
  }
  if(focusable) {
    h->widgets.at(id).focusable=true;
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(obj,onLvEvent,LV_EVENT_FOCUSED,(void *)(uintptr_t)id);
    lv_obj_add_event_cb(obj,onLvEvent,LV_EVENT_DEFOCUSED,(void *)(uintptr_t)id);
    lv_group_t *targetGroup=groupForWidget(h,id);
    lv_group_add_obj(targetGroup, obj);
    if (!lv_group_get_focused(targetGroup))
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
  if(it->second.kind=="richText"&&!validSpanProps(c,argv[1])) return JS_EXCEPTION;
  applyProps(h, it->second, argv[1]);
  if(has(c,argv[1],"focusable")) {
    JSValue v=prop(c,argv[1],"focusable"); bool focusable=JS_ToBool(c,v); JS_FreeValue(c,v);
    auto *o=it->second.obj;
    if(!focusable) { if(lv_obj_get_group(o)) lv_group_remove_obj(o); it->second.focusable=false; }
    else { it->second.focusable=true; if(visibleWidget(h,(uint32_t)id)&&!lv_obj_get_group(o)) { lv_group_add_obj(groupForWidget(h,(uint32_t)id),o); lv_obj_add_flag(o,LV_OBJ_FLAG_CLICK_FOCUSABLE); } }
  } else if(it->second.focusable&&!lv_obj_get_group(it->second.obj)&&visibleWidget(h,(uint32_t)id)) lv_group_add_obj(groupForWidget(h,(uint32_t)id),it->second.obj);
  return JS_UNDEFINED;
}
static JSValue cpCommand(JSContext *c, JSValueConst, int argc, JSValueConst *argv) {
  int32_t id=0; if (argc < 2 || JS_ToInt32(c,&id,argv[0])<0) return JS_ThrowTypeError(c,"command(id,action,arg?) expected");
  auto it=G->widgets.find((uint32_t)id); if(it==G->widgets.end()) return JS_ThrowReferenceError(c,"widget %d is disposed",id);
  Widget &w=it->second; std::string action=str(c,argv[1]);
  if(action=="focus") { if(lv_obj_get_group(w.obj)==G->group) { lv_group_focus_obj(w.obj); lv_obj_scroll_to_view(w.obj,LV_ANIM_OFF); } return JS_UNDEFINED; }
  if(action=="isFocused") return JS_NewBool(c,lv_obj_get_group(w.obj)==G->group&&lv_group_get_focused(G->group)==w.obj);
  if(action=="trapFocus") { uint32_t initial=0; if(argc>2) JS_ToUint32(c,&initial,argv[2]); if(!beginFocusScope(G,(uint32_t)id,initial)) return JS_ThrowTypeError(c,"focus scope needs visible focusable descendants and a valid initial widget"); return JS_UNDEFINED; }
  if(action=="releaseFocus") { if(!endFocusScope(G,(uint32_t)id)) return JS_ThrowTypeError(c,"focus scopes must be released in nesting order"); return JS_UNDEFINED; }
  if(action=="getValue") return JS_NewString(c,w.kind=="input"?lv_textarea_get_text(w.obj):"");
  if(action=="getScrollY") return JS_NewInt32(c,lv_obj_get_scroll_y(w.obj));
  int32_t n=0; if(argc>2) JS_ToInt32(c,&n,argv[2]);
  if(action=="scrollTo") lv_obj_scroll_to_y(w.obj,n,LV_ANIM_OFF);
  else if(action=="scrollBy") lv_obj_scroll_by(w.obj,0,n,LV_ANIM_OFF);
  else if(action=="scrollToEnd") lv_obj_scroll_to_y(w.obj,LV_COORD_MAX,LV_ANIM_OFF);
  else return JS_ThrowTypeError(c,"unknown widget command '%s'",action.c_str());
  return JS_UNDEFINED;
}
static void eraseTree(Host *h, uint32_t id) {
  auto it = h->widgets.find(id);
  if (it == h->widgets.end())
    return;
  auto kids = it->second.children;
  for (uint32_t child : kids)
    eraseTree(h, child);
  for(auto &scope:h->focusScopes) scope.members.erase(std::remove(scope.members.begin(),scope.members.end(),id),scope.members.end());
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
    while(!G->focusScopes.empty()&&insideWidgetTree(G,G->focusScopes.back().container,(uint32_t)id)) endFocusScope(G,0);
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
  if (G->async && G->async->handles(service))
    return G->async->invoke(c, service, method, json);
  if(service=="platform" && method=="get") {
    const char *bin=std::getenv("CPZERO_CODEX_BIN"); const char *cwd=std::getenv("CPZERO_CODEX_CWD");
    std::string wd=cwd&&*cwd?cwd:fs::current_path().string();
    std::string command=bin&&*bin?bin:"codex";
    auto quote=[](const std::string &s){std::string out="\""; for(char ch:s){if(ch=='\\'||ch=='\"')out.push_back('\\'); if(ch=='\n')out+="\\n"; else out.push_back(ch);} return out+"\"";};
    return JS_NewString(c,("{\"cwd\":"+quote(wd)+",\"codexCommand\":"+quote(command)+"}").c_str());
  }
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
  if(lv_display_flush_is_last(d)) h->framebuffer.present(h->pixels.data());
#else
  if (h->texture && lv_display_flush_is_last(d)) {
    SDL_UpdateTexture(h->texture, nullptr, h->pixels.data(),
                      W * sizeof(uint32_t));
    SDL_RenderClear(h->renderer);
    SDL_RenderCopy(h->renderer, h->texture, nullptr, nullptr);
    SDL_RenderPresent(h->renderer);
  }
#endif
  lv_display_flush_ready(d);
}
static bool cpKeyConsumed(const std::string &key, bool ctrl, bool shift, bool alt, bool meta) {
  JSValue global=JS_GetGlobalObject(G->ctx), fn=JS_GetPropertyStr(G->ctx,global,"__cpKey"); JS_FreeValue(G->ctx,global);
  if(!JS_IsFunction(G->ctx,fn)) { JS_FreeValue(G->ctx,fn); return false; }
  JSValue m=JS_NewObject(G->ctx);
  JS_SetPropertyStr(G->ctx,m,"ctrl",JS_NewBool(G->ctx,ctrl)); JS_SetPropertyStr(G->ctx,m,"shift",JS_NewBool(G->ctx,shift));
  JS_SetPropertyStr(G->ctx,m,"alt",JS_NewBool(G->ctx,alt)); JS_SetPropertyStr(G->ctx,m,"meta",JS_NewBool(G->ctx,meta));
  JSValue a[2]={JS_NewString(G->ctx,key.c_str()),m};
  G->deadline=Clock::now()+std::chrono::milliseconds(JS_BUDGET_MS); G->deadlineActive=true;
  JSValue r=JS_Call(G->ctx,fn,JS_UNDEFINED,2,a); bool ok=check(G->ctx,r,"__cpKey");
  bool consumed=ok&&JS_ToBool(G->ctx,r); JS_FreeValue(G->ctx,r);
  ok=drainJobs(G,"__cpKey"); G->deadlineActive=false; if(!ok) G->running=false;
  JS_FreeValue(G->ctx,a[0]); JS_FreeValue(G->ctx,a[1]); JS_FreeValue(G->ctx,fn); return consumed;
}
#ifndef CPZERO_FBDEV
static void pointerRead(lv_indev_t *, lv_indev_data_t *d) {
  if(!G->pointerEvents.empty()) {
    PointerSample sample=G->pointerEvents.front(); G->pointerEvents.pop_front();
    G->pointerX=sample.x; G->pointerY=sample.y; G->pointerPressed=sample.pressed;
    if(std::getenv("CPZERO_DEBUG_INPUT")) std::cerr<<"pointer sample logical="<<sample.x<<","<<sample.y<<" pressed="<<sample.pressed<<" remaining="<<G->pointerEvents.size()<<"\n";
  }
  d->point.x=G->pointerX; d->point.y=G->pointerY;
  d->state=G->pointerPressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
  d->continue_reading=!G->pointerEvents.empty();
}
static void flushPointerEvents(Host *h) {
  if(h->mouse && !h->pointerEvents.empty()) lv_indev_read(h->mouse);
}
static std::string keyName(SDL_Keycode k) {
  switch(k) { case SDLK_RETURN: case SDLK_KP_ENTER:return "Enter"; case SDLK_ESCAPE:return "Escape"; case SDLK_TAB:return "Tab"; case SDLK_UP:return "ArrowUp"; case SDLK_DOWN:return "ArrowDown"; case SDLK_LEFT:return "ArrowLeft"; case SDLK_RIGHT:return "ArrowRight"; case SDLK_PAGEUP:return "PageUp"; case SDLK_PAGEDOWN:return "PageDown"; case SDLK_HOME:return "Home"; case SDLK_END:return "End"; default: return k>=32&&k<127?std::string(1,(char)std::tolower((unsigned char)k)):SDL_GetKeyName(k); }
}
static bool shortcutConsumed(SDL_Keycode k, SDL_Keymod mods) {
  return cpKeyConsumed(keyName(k),(mods&KMOD_CTRL)!=0,(mods&KMOD_SHIFT)!=0,(mods&KMOD_ALT)!=0,(mods&KMOD_GUI)!=0);
}
static lv_point_t logicalPoint(Host *,int x,int y) {
  // SDL_RenderSetLogicalSize filters mouse events into logical coordinates.
  return {(lv_coord_t)x,(lv_coord_t)y};
}
static void enqueuePointer(Host *h,int x,int y,bool pressed,bool edge) {
  lv_point_t p=logicalPoint(h,x,y);
  constexpr size_t LIMIT=2048;
  if(h->pointerEvents.size()>=LIMIT) {
    if(!edge) {
      auto it=std::find_if(h->pointerEvents.rbegin(),h->pointerEvents.rend(),[](const PointerSample &s){return !s.edge;});
      if(it!=h->pointerEvents.rend()) { *it={p.x,p.y,pressed,false}; return; }
      return;
    }
    auto it=std::find_if(h->pointerEvents.begin(),h->pointerEvents.end(),[](const PointerSample &s){return !s.edge;});
    if(it!=h->pointerEvents.end()) h->pointerEvents.erase(it);
    else { std::cerr<<"SDL pointer input queue full; dropping pointer edge\n"; return; }
  }
  h->pointerEvents.push_back({p.x,p.y,pressed,edge});
}
static void routeSdlEvent(Host *h,const SDL_Event &e) {
  if(e.type==SDL_QUIT) { h->running=false; return; }
  if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
    h->keyEvents.clear(); h->heldKey=0; h->heldKeyPressed=false;
    if(h->pointerPressed) enqueuePointer(h,h->pointerX,h->pointerY,false,true);
    return;
  }
  if(e.type==SDL_MOUSEMOTION) { if(std::getenv("CPZERO_DEBUG_INPUT")){auto p=logicalPoint(h,e.motion.x,e.motion.y);std::cerr<<"SDL motion raw="<<e.motion.x<<","<<e.motion.y<<" logical="<<p.x<<","<<p.y<<"\n";} enqueuePointer(h,e.motion.x,e.motion.y,(e.motion.state&SDL_BUTTON_LMASK)!=0,false); return; }
  if(e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP) {
    if(e.button.button==SDL_BUTTON_LEFT) { if(std::getenv("CPZERO_DEBUG_INPUT")){auto p=logicalPoint(h,e.button.x,e.button.y);int lx=0,ly=0,gx=0,gy=0,wx=0,wy=0;Uint32 buttons=SDL_GetMouseState(&lx,&ly);Uint32 globalButtons=SDL_GetGlobalMouseState(&gx,&gy);SDL_GetWindowPosition(h->window,&wx,&wy);std::cerr<<"SDL button "<<(e.type==SDL_MOUSEBUTTONDOWN?"down":"up")<<" raw="<<e.button.x<<","<<e.button.y<<" logical="<<p.x<<","<<p.y<<" clicks="<<(int)e.button.clicks<<" eventWindow="<<e.button.windowID<<" ownWindow="<<SDL_GetWindowID(h->window)<<" localNow="<<lx<<","<<ly<<" buttons="<<buttons<<" globalNow="<<gx<<","<<gy<<" globalButtons="<<globalButtons<<" windowOrigin="<<wx<<","<<wy<<" mouseFocus="<<(SDL_GetMouseFocus()==h->window)<<"\n";} enqueuePointer(h,e.button.x,e.button.y,e.type==SDL_MOUSEBUTTONDOWN,true); }
    return;
  }
  if(e.type==SDL_MOUSEWHEEL) {
    lv_point_t p=logicalPoint(h,e.wheel.mouseX,e.wheel.mouseY); lv_obj_t *best=nullptr; int bestArea=std::numeric_limits<int>::max();
    for(auto &entry:h->widgets) { auto *o=entry.second.obj; lv_dir_t dir=lv_obj_get_scroll_dir(o); if(!(dir&LV_DIR_VER)||!lv_obj_hit_test(o,&p)) continue; lv_area_t a; lv_obj_get_coords(o,&a); int area=(a.x2-a.x1+1)*(a.y2-a.y1+1); if(area<bestArea){best=o;bestArea=area;} }
    if(best) { int delta=e.wheel.y*24; if(e.wheel.direction!=SDL_MOUSEWHEEL_FLIPPED) delta=-delta; lv_obj_scroll_by(best,0,delta,LV_ANIM_OFF); }
    return;
  }
  if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_F12&&!h->screenshot.empty()) { h->screenshotRequested=true; return; }
  if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP||e.type==SDL_TEXTINPUT) {
    flushPointerEvents(h);
    if(h->keyEvents.size()<1024) h->keyEvents.push_back(e);
    else std::cerr<<"SDL keyboard input queue full; dropping input event\n";
  }
}
static void pollSdlEvents(Host *h) {
  if(h->screen) lv_obj_update_layout(h->screen);
  SDL_Event e; bool injectedText=false;
  while(SDL_PollEvent(&e)) {
    if(!injectedText && (e.type==SDL_KEYDOWN||e.type==SDL_KEYUP||e.type==SDL_TEXTINPUT)) {
      while(!h->syntheticEvents.empty()) { SDL_Event pending=h->syntheticEvents.front(); h->syntheticEvents.pop_front(); routeSdlEvent(h,pending); }
      injectedText=true;
    }
    routeSdlEvent(h,e);
  }
  while(!h->syntheticEvents.empty()) { e=h->syntheticEvents.front(); h->syntheticEvents.pop_front(); routeSdlEvent(h,e); }
}
static void pushText(Host *h, const std::string &text) {
  for(size_t i=0;i<text.size();) {
    SDL_Event e{}; e.type=SDL_TEXTINPUT;
    size_t n=std::min<size_t>(sizeof(e.text.text)-1,text.size()-i);
    while(n && i+n<text.size() && (static_cast<unsigned char>(text[i+n])&0xc0)==0x80) --n;
    if(!n) n=std::min<size_t>(sizeof(e.text.text)-1,text.size()-i);
    std::memcpy(e.text.text,text.data()+i,n); e.text.text[n]='\0';
    h->syntheticEvents.push_back(e);
    i+=n;
  }
}
static void pushClick(Host *h) {
  if(h->clickInjected || h->clickX<0 || !h->window) return;
  const int x=h->clickX*h->scale,y=h->clickY*h->scale;
  SDL_Event down{}; down.type=SDL_MOUSEBUTTONDOWN; down.button.button=SDL_BUTTON_LEFT; down.button.state=SDL_PRESSED; down.button.x=x; down.button.y=y; down.button.windowID=SDL_GetWindowID(h->window);
  SDL_Event up=down; up.type=SDL_MOUSEBUTTONUP; up.button.state=SDL_RELEASED;
  for(int i=0;i<2;++i) if(SDL_PushEvent(&down)<0 || SDL_PushEvent(&up)<0) std::cerr<<"SDL click injection failed: "<<SDL_GetError()<<"\n";
  h->clickInjected=true;
}
static void pushKeys(Host *h) {
  if(h->keysBatch) {
    if(h->keysInjected) return;
    for(int raw:h->keys) {
      SDL_Keycode k=raw==HOST_KEY_ENTER?SDLK_RETURN:raw==HOST_KEY_TAB||raw==HOST_KEY_SHIFTTAB?SDLK_TAB:raw==HOST_KEY_ESCAPE?SDLK_ESCAPE:raw==HOST_KEY_UP?SDLK_UP:raw==HOST_KEY_DOWN?SDLK_DOWN:raw==HOST_KEY_LEFT?SDLK_LEFT:raw==HOST_KEY_RIGHT?SDLK_RIGHT:raw==HOST_KEY_PAGEUP?SDLK_PAGEUP:raw==HOST_KEY_PAGEDOWN?SDLK_PAGEDOWN:raw==HOST_KEY_HOME?SDLK_HOME:raw==HOST_KEY_END?SDLK_END:raw==HOST_KEY_BACKSPACE?SDLK_BACKSPACE:(SDL_Keycode)raw;
      for(bool down:{true,false}) { SDL_Event e{}; e.type=down?SDL_KEYDOWN:SDL_KEYUP; e.key.state=down?SDL_PRESSED:SDL_RELEASED; e.key.keysym.sym=k; e.key.keysym.mod=raw==HOST_KEY_SHIFTTAB?KMOD_SHIFT:KMOD_NONE; if(SDL_PushEvent(&e)<0) std::cerr<<"SDL key injection failed: "<<SDL_GetError()<<"\n"; }
    }
    h->keysInjected=1; return;
  }
  bool down=false;
  if(h->keyReleasePending) { h->keyReleasePending=false; }
  else {
    if(h->keysInjected>=h->keys.size()) return;
    int raw=h->keys[h->keysInjected++];
    h->injectedKey=raw==HOST_KEY_ENTER?SDLK_RETURN:raw==HOST_KEY_TAB||raw==HOST_KEY_SHIFTTAB?SDLK_TAB:raw==HOST_KEY_ESCAPE?SDLK_ESCAPE:raw==HOST_KEY_UP?SDLK_UP:raw==HOST_KEY_DOWN?SDLK_DOWN:raw==HOST_KEY_LEFT?SDLK_LEFT:raw==HOST_KEY_RIGHT?SDLK_RIGHT:raw==HOST_KEY_PAGEUP?SDLK_PAGEUP:raw==HOST_KEY_PAGEDOWN?SDLK_PAGEDOWN:raw==HOST_KEY_HOME?SDLK_HOME:raw==HOST_KEY_END?SDLK_END:raw==HOST_KEY_BACKSPACE?SDLK_BACKSPACE:(SDL_Keycode)raw;
    h->injectedMods=raw==HOST_KEY_SHIFTTAB?KMOD_SHIFT:KMOD_NONE; h->keyReleasePending=true; down=true;
  }
  SDL_Event e{}; e.type=down?SDL_KEYDOWN:SDL_KEYUP; e.key.state=down?SDL_PRESSED:SDL_RELEASED; e.key.repeat=0; e.key.keysym.sym=h->injectedKey; e.key.keysym.mod=h->injectedMods;
  if(SDL_PushEvent(&e)<0) std::cerr<<"SDL key injection failed: "<<SDL_GetError()<<"\n";
}
static void textInput(const char *s) {
  if(!G->suppressedText.empty() && SDL_GetTicks()<=G->suppressTextUntil && G->suppressedText==s) { G->suppressedText.clear(); return; }
  G->suppressedText.clear();
  lv_obj_t *focused=lv_group_get_focused(G->group); if(!focused) return;
  auto *ta=lv_obj_check_type(focused,&lv_textarea_class)?focused:nullptr;
  if(ta) lv_textarea_add_text(ta,s);
}
static uint32_t mapKey(SDL_Keycode k, SDL_Keymod mods) {
  switch (k) {
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return LV_KEY_ENTER;
  case SDLK_TAB:
    return (mods & KMOD_SHIFT) ? LV_KEY_PREV : LV_KEY_NEXT;
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
  case SDLK_PAGEUP:return LV_KEY_PREV;
  case SDLK_PAGEDOWN:return LV_KEY_NEXT;
  case SDLK_HOME:return LV_KEY_HOME;
  case SDLK_END:return LV_KEY_END;
  default:
    return 0;
  }
}
#endif
#ifdef CPZERO_FBDEV
static void keyRead(lv_indev_t *, lv_indev_data_t *d) {
  uint32_t nextKey = 0;
  bool nextPressed = false;
  bool readMore = G->framebuffer.readKey(nextKey, nextPressed);
  if (readMore) {
    G->heldKey = nextKey;
    G->heldKeyPressed = nextPressed;
    if(G->heldKeyPressed) { bool ctrl=false,shift=false,alt=false,meta=false; G->framebuffer.getModifiers(ctrl,shift,alt,meta); if(cpKeyConsumed(G->framebuffer.lastKeyName(),ctrl,shift,alt,meta)) { G->heldKey=0; G->heldKeyPressed=false; } }
  }
  d->key = G->heldKey;
  d->state = G->heldKeyPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  d->continue_reading = readMore;
}
#else
static void keyRead(lv_indev_t *, lv_indev_data_t *d) {
  d->continue_reading = false;
  while (!G->keyEvents.empty()) {
    SDL_Event e=G->keyEvents.front(); G->keyEvents.pop_front();
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
      if (e.type == SDL_KEYDOWN && shortcutConsumed(e.key.keysym.sym, (SDL_Keymod)e.key.keysym.mod)) {
        SDL_Keycode k=e.key.keysym.sym;
        if(k>=32&&k<127) { char ch=(char)k; if((e.key.keysym.mod&KMOD_SHIFT)&&std::isalpha((unsigned char)ch)) ch=(char)std::toupper((unsigned char)ch); G->suppressedText.assign(1,ch); G->suppressTextUntil=SDL_GetTicks()+150; }
        continue;
      }
      if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) && e.key.keysym.sym == SDLK_v) {
        char *clip=SDL_GetClipboardText(); if(clip){ textInput(clip); SDL_free(clip); } continue;
      }
      uint32_t key = mapKey(e.key.keysym.sym,(SDL_Keymod)e.key.keysym.mod);
      if(e.key.keysym.sym==SDLK_SPACE) { lv_obj_t *focused=lv_group_get_focused(G->group); if(focused&&lv_obj_check_type(focused,&lv_button_class)) key=LV_KEY_ENTER; }
      if (!key)
        continue;
      G->heldKey = key;
      d->key = key;
      d->state = e.type == SDL_KEYDOWN ? LV_INDEV_STATE_PRESSED
                                       : LV_INDEV_STATE_RELEASED;
      G->heldKeyPressed = d->state == LV_INDEV_STATE_PRESSED;
      d->continue_reading = !G->keyEvents.empty();
      return;
    }
    if(e.type==SDL_TEXTINPUT) { textInput(e.text.text); continue; }
  }
  d->key = G->heldKey;
  d->state = G->heldKeyPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
  JS_SetPropertyStr(h->ctx, cp, "command", JS_NewCFunction(h->ctx, cpCommand, "command", 3));
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
  if (h->async) { h->async->shutdown(); h->async.reset(); }
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
  h->async = std::make_unique<cpzero::AsyncServices>();
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
  lv_obj_set_style_bg_color(h->screen, lv_color_hex(0x181818), 0);
  lv_obj_set_style_text_color(h->screen,lv_color_hex(0xe6e6e6),0);
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
  if(auto *timer=lv_indev_get_read_timer(h->keyboard)) lv_timer_set_period(timer,8);
  lv_indev_set_group(h->keyboard, h->group);
  return true;
}
#ifndef CPZERO_FBDEV
static bool setupSdl(Host *h) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH,"1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL: " << SDL_GetError() << "\n";
    return false;
  }
  if (!h->headless || h->clickX>=0)
    h->window = SDL_CreateWindow("CPZeroJS", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, W * h->scale,
                                 H * h->scale, h->headless?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN);
  if (h->window) {
    h->renderer = SDL_CreateRenderer(
        h->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!h->renderer)
      h->renderer = SDL_CreateRenderer(h->window, -1, SDL_RENDERER_SOFTWARE);
    if (!h->renderer)
      return false;
    SDL_RenderSetLogicalSize(h->renderer, W, H);
    if(std::getenv("CPZERO_DEBUG_INPUT")){int ww=0,wh=0,rw=0,rh=0,lw=0,lh=0;SDL_GetWindowSize(h->window,&ww,&wh);SDL_GetRendererOutputSize(h->renderer,&rw,&rh);SDL_RenderGetLogicalSize(h->renderer,&lw,&lh);std::cerr<<"SDL geometry window="<<ww<<"x"<<wh<<" output="<<rw<<"x"<<rh<<" logical="<<lw<<"x"<<lh<<" scale="<<h->scale<<"\n";}
    h->texture = SDL_CreateTexture(h->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, W, H);
  }
  SDL_StartTextInput();
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
  while(!h->focusScopes.empty()) endFocusScope(h,0);
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
    else if (a == "--text" && i + 1 < argc)
      h.injectText = argv[++i];
    else if (a == "--click" && i + 1 < argc) {
      std::string pos=argv[++i]; auto comma=pos.find(',');
      if(comma==std::string::npos) { usage(); return 2; }
      h.clickX=std::clamp(std::atoi(pos.substr(0,comma).c_str()),0,W-1);
      h.clickY=std::clamp(std::atoi(pos.substr(comma+1).c_str()),0,H-1);
    }
    else if (a == "--keys" && i + 1 < argc) {
      std::stringstream ss(argv[++i]);
      std::string k;
      while (std::getline(ss, k, ',')) {
        if (k == "Enter")
          h.keys.push_back(HOST_KEY_ENTER);
        else if (k == "Tab")
          h.keys.push_back(HOST_KEY_TAB);
        else if (k == "ShiftTab") h.keys.push_back(HOST_KEY_SHIFTTAB);
        else if (k == "Escape")
          h.keys.push_back(HOST_KEY_ESCAPE);
        else if(k=="ArrowUp") h.keys.push_back(HOST_KEY_UP);
        else if(k=="ArrowDown") h.keys.push_back(HOST_KEY_DOWN);
        else if(k=="ArrowLeft") h.keys.push_back(HOST_KEY_LEFT);
        else if(k=="ArrowRight") h.keys.push_back(HOST_KEY_RIGHT);
        else if(k=="PageUp") h.keys.push_back(HOST_KEY_PAGEUP);
        else if(k=="PageDown") h.keys.push_back(HOST_KEY_PAGEDOWN);
        else if(k=="Home") h.keys.push_back(HOST_KEY_HOME);
        else if(k=="End") h.keys.push_back(HOST_KEY_END);
        else if(k=="Backspace") h.keys.push_back(HOST_KEY_BACKSPACE);
        else if (k.size() == 1)
          h.keys.push_back((int)k[0]);
      }
    } else if (a == "--keys-batch" && i + 1 < argc) {
      h.keysBatch=true;
      std::stringstream ss(argv[++i]); std::string k;
      while(std::getline(ss,k,',')) { if(k=="Enter")h.keys.push_back(HOST_KEY_ENTER); else if(k=="Tab")h.keys.push_back(HOST_KEY_TAB); else if(k=="ShiftTab")h.keys.push_back(HOST_KEY_SHIFTTAB); else if(k=="Escape")h.keys.push_back(HOST_KEY_ESCAPE); else if(k=="ArrowUp")h.keys.push_back(HOST_KEY_UP); else if(k=="ArrowDown")h.keys.push_back(HOST_KEY_DOWN); else if(k.size()==1)h.keys.push_back((int)(unsigned char)k[0]); }
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
  lv_obj_update_layout(h.screen);
  auto stamp = fs::last_write_time(h.bundle);
  auto reloadAt = Clock::now();
  while (h.running && (h.frames < 0 || h.frame < h.frames)) {
    auto start = Clock::now();
#ifndef CPZERO_FBDEV
    pushClick(&h);
    pushKeys(&h);
    if(!h.injectText.empty()&&!h.textInjected) { pushText(&h,h.injectText); h.textInjected=true; }
    pollSdlEvents(&h);
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
#ifndef CPZERO_FBDEV
    if (!h.keyEvents.empty()) lv_indev_read(h.keyboard);
    flushPointerEvents(&h);
#endif
    lv_timer_handler();
    if(h.screenshotRequested) { if(!screenshotWrite(&h,h.screenshot)) std::cerr<<"Could not write screenshot: "<<h.screenshot<<"\n"; else std::cerr<<"Screenshot saved: "<<h.screenshot<<"\n"; h.screenshotRequested=false; }
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
          { lv_obj_update_layout(h.screen); std::cerr << "Reloaded " << h.bundle << "\n"; }
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
  if (h.async) { h.async->shutdown(); h.async.reset(); }
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
