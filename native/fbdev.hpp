#pragma once

// Linux-only framebuffer and evdev adapter. No device-specific GPIO guesses.
// The target image must expose a 320x170 framebuffer and a keyboard evdev node.
#ifdef __linux__
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cpzero {
class Framebuffer {
  int fd_ = -1;
  int keyboard_ = -1;
  uint8_t *mapped_ = nullptr;
  size_t mappedSize_ = 0;
  fb_var_screeninfo info_{};
  fb_fix_screeninfo fixed_{};
  bool shift_ = false;

  static uint32_t channel(uint32_t value, fb_bitfield field) {
    if (field.length == 0)
      return 0;
    const uint64_t max = (uint64_t{1} << field.length) - 1;
    return static_cast<uint32_t>((value * max / 255) << field.offset);
  }

  void release() {
    if (mapped_)
      munmap(mapped_, mappedSize_);
    if (fd_ >= 0)
      close(fd_);
    if (keyboard_ >= 0)
      close(keyboard_);
    mapped_ = nullptr;
    fd_ = keyboard_ = -1;
  }

  static int openKeyboard(const std::filesystem::path &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
      return -1;
    unsigned char bits[(KEY_MAX + 8) / 8]{};
    const auto has = [&](int code) {
      return (bits[code / 8] & (1 << (code % 8))) != 0;
    };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0 || !has(KEY_A) ||
        !has(KEY_ENTER)) {
      close(fd);
      return -1;
    }
    return fd;
  }

public:
  Framebuffer() = default;
  Framebuffer(const Framebuffer &) = delete;
  Framebuffer &operator=(const Framebuffer &) = delete;
  ~Framebuffer() { release(); }

  void openDevices(const char *framebuffer, const char *keyboard) {
    try {
      fd_ = open(framebuffer && *framebuffer ? framebuffer : "/dev/fb0",
                 O_RDWR | O_CLOEXEC);
      if (fd_ < 0)
        throw std::runtime_error("Cannot open framebuffer: " +
                                 std::string(strerror(errno)));
      if (ioctl(fd_, FBIOGET_VSCREENINFO, &info_) < 0 ||
          ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) < 0)
        throw std::runtime_error("Cannot inspect framebuffer geometry");
      if (info_.xres != 320 || info_.yres != 170 ||
          (info_.bits_per_pixel != 16 && info_.bits_per_pixel != 32) ||
          fixed_.type != FB_TYPE_PACKED_PIXELS ||
          fixed_.visual != FB_VISUAL_TRUECOLOR)
        throw std::runtime_error("CPZeroJS requires a 320x170 packed truecolor "
                                 "framebuffer (16 or 32 bits)");
      for (auto field : {info_.red, info_.green, info_.blue, info_.transp}) {
        if (field.length > 8 ||
            field.offset + field.length > info_.bits_per_pixel ||
            field.msb_right)
          throw std::runtime_error("Unsupported framebuffer channel layout");
      }
      const uint64_t rowEnd =
          (uint64_t(info_.xoffset) + 320) * (info_.bits_per_pixel / 8);
      const uint64_t lastEnd =
          (uint64_t(info_.yoffset) + 169) * fixed_.line_length + rowEnd;
      if (rowEnd > fixed_.line_length || lastEnd > fixed_.smem_len)
        throw std::runtime_error("Framebuffer offsets exceed mapped memory");
      mappedSize_ = fixed_.smem_len;
      void *memory = mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd_, 0);
      if (memory == MAP_FAILED)
        throw std::runtime_error("Cannot map framebuffer");
      mapped_ = static_cast<uint8_t *>(memory);
      if (keyboard && *keyboard)
        keyboard_ = openKeyboard(keyboard);
      else {
        std::error_code ec;
        for (const auto &entry :
             std::filesystem::directory_iterator("/dev/input", ec)) {
          if (entry.path().filename().string().rfind("event", 0) != 0)
            continue;
          keyboard_ = openKeyboard(entry.path());
          if (keyboard_ >= 0)
            break;
        }
      }
      if (keyboard_ < 0)
        throw std::runtime_error("No accessible evdev keyboard; set "
                                 "CPZERO_INPUT_DEVICE and check permissions");
    } catch (...) {
      release();
      throw;
    }
  }

  void present(const uint32_t *pixels) {
    const unsigned bytes = info_.bits_per_pixel / 8;
    for (unsigned y = 0; y < 170; ++y) {
      auto *dst = mapped_ + (y + info_.yoffset) * fixed_.line_length +
                  info_.xoffset * bytes;
      for (unsigned x = 0; x < 320; ++x) {
        const uint32_t p = pixels[y * 320 + x];
        const uint32_t native = channel((p >> 16) & 255, info_.red) |
                                channel((p >> 8) & 255, info_.green) |
                                channel(p & 255, info_.blue) |
                                channel(255, info_.transp);
        if (bytes == 2) {
          const uint16_t shortPixel = static_cast<uint16_t>(native);
          std::memcpy(dst + x * bytes, &shortPixel, 2);
        } else
          std::memcpy(dst + x * bytes, &native, 4);
      }
    }
  }

  // Returns one event at a time, allowing the host/LVGL to preserve
  // press/release order.
  bool readKey(uint32_t &key, bool &pressed) {
    input_event event{};
    while (read(keyboard_, &event, sizeof(event)) == sizeof(event)) {
      if (event.type != EV_KEY)
        continue;
      if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT) {
        shift_ = event.value != 0;
        continue;
      }
      switch (event.code) {
      case KEY_ENTER:
        key = LV_KEY_ENTER;
        break;
      case KEY_TAB:
        key = shift_ ? LV_KEY_PREV : LV_KEY_NEXT;
        break;
      case KEY_ESC:
        key = LV_KEY_ESC;
        break;
      case KEY_BACKSPACE:
        key = LV_KEY_BACKSPACE;
        break;
      case KEY_DELETE:
        key = LV_KEY_DEL;
        break;
      case KEY_LEFT:
        key = LV_KEY_LEFT;
        break;
      case KEY_RIGHT:
        key = LV_KEY_RIGHT;
        break;
      case KEY_UP:
        key = LV_KEY_UP;
        break;
      case KEY_DOWN:
        key = LV_KEY_DOWN;
        break;
      case KEY_HOME:
        key = LV_KEY_HOME;
        break;
      case KEY_END:
        key = LV_KEY_END;
        break;
      case KEY_SPACE:
        key = ' ';
        break;
      default: {
        static constexpr char top[] = "qwertyuiop";
        static constexpr char middle[] = "asdfghjkl";
        static constexpr char bottom[] = "zxcvbnm";
        if (event.code >= KEY_Q && event.code <= KEY_P)
          key = top[event.code - KEY_Q];
        else if (event.code >= KEY_A && event.code <= KEY_L)
          key = middle[event.code - KEY_A];
        else if (event.code >= KEY_Z && event.code <= KEY_M)
          key = bottom[event.code - KEY_Z];
        else if (event.code >= KEY_1 && event.code <= KEY_0)
          key = (shift_ ? "!@#$%^&*()" : "1234567890")[event.code - KEY_1];
        else {
          switch (event.code) {
          case KEY_MINUS:
            key = shift_ ? '_' : '-';
            break;
          case KEY_EQUAL:
            key = shift_ ? '+' : '=';
            break;
          case KEY_LEFTBRACE:
            key = shift_ ? '{' : '[';
            break;
          case KEY_RIGHTBRACE:
            key = shift_ ? '}' : ']';
            break;
          case KEY_SEMICOLON:
            key = shift_ ? ':' : ';';
            break;
          case KEY_APOSTROPHE:
            key = shift_ ? '"' : '\'';
            break;
          case KEY_GRAVE:
            key = shift_ ? '~' : '`';
            break;
          case KEY_BACKSLASH:
            key = shift_ ? '|' : '\\';
            break;
          case KEY_COMMA:
            key = shift_ ? '<' : ',';
            break;
          case KEY_DOT:
            key = shift_ ? '>' : '.';
            break;
          case KEY_SLASH:
            key = shift_ ? '?' : '/';
            break;
          default:
            continue;
          }
        }
        if (shift_ && key >= 'a' && key <= 'z')
          key -= 'a' - 'A';
      }
      }
      pressed = event.value != 0;
      return true;
    }
    return false;
  }
};
} // namespace cpzero
#endif
