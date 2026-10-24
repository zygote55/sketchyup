// A small streaming JSON writer for the helper's probe output. The helper has no Qt: it is the
// one executable that links IfcOpenShell (ADR 0171 decision 19), so it stays a plain C++ program.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sketchyup::ifc {

class JsonOut {
public:
  JsonOut& beginObject() { return open('{'); }
  JsonOut& endObject() { return close('}'); }
  JsonOut& beginArray() { return open('['); }
  JsonOut& endArray() { return close(']'); }

  JsonOut& key(std::string_view name) {
    separator();
    appendString(name);
    out_ += ':';
    pendingValue_ = true;
    return *this;
  }
  JsonOut& value(std::string_view text) {
    separator();
    appendString(text);
    return *this;
  }
  JsonOut& value(const char* text) { return value(std::string_view(text)); }
  JsonOut& value(bool flag) {
    separator();
    out_ += flag ? "true" : "false";
    return *this;
  }
  JsonOut& value(long long number) {
    separator();
    out_ += std::to_string(number);
    return *this;
  }
  JsonOut& value(int number) { return value(static_cast<long long>(number)); }
  JsonOut& value(std::size_t number) { return value(static_cast<long long>(number)); }
  // Any other integer type (long, unsigned, ...) goes through long long.
  template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, int> &&
                                          !std::is_same_v<T, long long> && !std::is_same_v<T, std::size_t>,
                                      int> = 0>
  JsonOut& value(T number) {
    return value(static_cast<long long>(number));
  }
  JsonOut& value(double number) {
    separator();
    if (!std::isfinite(number)) {
      out_ += "null";
      return *this;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.17g", number);
    out_ += buffer;
    return *this;
  }
  // Appends an already-formed JSON value.
  JsonOut& raw(std::string_view json) {
    separator();
    out_ += json;
    return *this;
  }
  // Closes every container opened after the first `depth`, so an exception caught mid-document
  // still leaves valid JSON.
  JsonOut& unwindTo(std::size_t depth) {
    if (pendingValue_) out_ += "null";  // a key was written and its value never came
    pendingValue_ = false;
    while (brackets_.size() > depth) {
      out_ += brackets_.back() == '{' ? '}' : ']';
      brackets_.pop_back();
      first_.pop_back();
    }
    return *this;
  }
  JsonOut& null() {
    separator();
    out_ += "null";
    return *this;
  }
  template <class T> JsonOut& field(std::string_view name, const T& v) { return key(name).value(v); }
  JsonOut& doubles(const std::vector<double>& numbers) {
    beginArray();
    for (double number : numbers) value(number);
    return endArray();
  }
  JsonOut& strings(const std::vector<std::string>& items) {
    beginArray();
    for (const auto& item : items) value(item);
    return endArray();
  }

  const std::string& str() const { return out_; }

private:
  JsonOut& open(char bracket) {
    separator();
    out_ += bracket;
    brackets_.push_back(bracket);
    first_.push_back(true);
    return *this;
  }
  JsonOut& close(char bracket) {
    out_ += bracket;
    brackets_.pop_back();
    first_.pop_back();
    return *this;
  }
  void separator() {
    if (pendingValue_) {
      pendingValue_ = false;
      markUsed();
      return;
    }
    if (!first_.empty()) {
      if (!first_.back()) out_ += ',';
      first_.back() = false;
    }
  }
  void markUsed() {
    if (!first_.empty()) first_.back() = false;
  }
  void appendString(std::string_view text) {
    out_ += '"';
    for (std::size_t i = 0; i < text.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      if (c >= 0x80) {
        // IFC strings are not guaranteed to be UTF-8: copy a valid sequence, replace anything else
        // with U+FFFD, so the output is always valid JSON.
        const std::size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
        bool valid = length != 0 && c < 0xF5 && i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k) valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (valid) {
          out_.append(text.substr(i, length));
          i += length - 1;
        } else {
          out_ += "\xEF\xBF\xBD";
        }
        continue;
      }
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        default:
          if (c < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
            out_ += buffer;
          } else {
            out_ += static_cast<char>(c);
          }
      }
    }
    out_ += '"';
  }

  std::string out_;
  std::vector<bool> first_;
  std::vector<char> brackets_;
  bool pendingValue_ = false;
};

}  // namespace sketchyup::ifc
