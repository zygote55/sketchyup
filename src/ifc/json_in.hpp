// A small bounded JSON reader for the helper's export package (R097.d). The helper has no Qt and
// reads one document written by the application; it still treats it as data: the size, the depth,
// the number of values and every string are bounded, and a malformed document is an error, not a
// guess. Numbers are doubles (strtod on the token), so an integer above 2^53 is not exact; the
// package never carries one.
#pragma once

#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace sketchyup::ifc {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;  // in document order

struct JsonValue {
  std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<JsonArray>, std::shared_ptr<JsonObject>> data{
      nullptr};

  bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
  bool isBool() const { return std::holds_alternative<bool>(data); }
  bool isNumber() const { return std::holds_alternative<double>(data); }
  bool isString() const { return std::holds_alternative<std::string>(data); }
  bool isArray() const { return std::holds_alternative<std::shared_ptr<JsonArray>>(data); }
  bool isObject() const { return std::holds_alternative<std::shared_ptr<JsonObject>>(data); }

  bool asBool() const {
    if (!isBool()) throw std::runtime_error("expected a boolean");
    return std::get<bool>(data);
  }
  double asNumber() const {
    if (!isNumber()) throw std::runtime_error("expected a number");
    return std::get<double>(data);
  }
  long long asInteger() const {
    const double number = asNumber();
    if (!std::isfinite(number) || number != std::floor(number) || std::abs(number) > 9007199254740992.0)
      throw std::runtime_error("expected an integer");
    return static_cast<long long>(number);
  }
  const std::string& asString() const {
    if (!isString()) throw std::runtime_error("expected a string");
    return std::get<std::string>(data);
  }
  const JsonArray& asArray() const {
    if (!isArray()) throw std::runtime_error("expected an array");
    return *std::get<std::shared_ptr<JsonArray>>(data);
  }
  const JsonObject& asObject() const {
    if (!isObject()) throw std::runtime_error("expected an object");
    return *std::get<std::shared_ptr<JsonObject>>(data);
  }
  // The member of an object, or nullptr.
  const JsonValue* find(std::string_view key) const {
    for (const auto& [name, value] : asObject())
      if (name == key) return &value;
    return nullptr;
  }
  const JsonValue& at(std::string_view key) const {
    const JsonValue* found = find(key);
    if (found == nullptr) throw std::runtime_error("missing member " + std::string(key));
    return *found;
  }
};

struct JsonLimits {
  std::size_t depth = 32;
  std::size_t values = 8'000'000;
  std::size_t stringBytes = 64 * 1024;
};

class JsonReader {
public:
  JsonReader(std::string_view text, JsonLimits limits) : text_(text), limits_(limits) {}

  JsonValue parse() {
    JsonValue value = readValue(0);
    skipSpace();
    if (at_ != text_.size()) fail("trailing characters after the document");
    return value;
  }

private:
  [[noreturn]] void fail(const std::string& message) const {
    throw std::runtime_error("JSON: " + message + " at byte " + std::to_string(at_));
  }
  void skipSpace() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) ++at_;
  }
  bool consume(char expected) {
    skipSpace();
    if (at_ < text_.size() && text_[at_] == expected) {
      ++at_;
      return true;
    }
    return false;
  }
  void count() {
    if (++values_ > limits_.values) fail("too many values");
  }

  JsonValue readValue(std::size_t depth) {
    if (depth > limits_.depth) fail("nested too deeply");
    skipSpace();
    if (at_ >= text_.size()) fail("unexpected end");
    count();
    const char c = text_[at_];
    if (c == '{') return readObject(depth);
    if (c == '[') return readArray(depth);
    if (c == '"') return JsonValue{readString()};
    if (c == 't' && text_.substr(at_, 4) == "true") {
      at_ += 4;
      return JsonValue{true};
    }
    if (c == 'f' && text_.substr(at_, 5) == "false") {
      at_ += 5;
      return JsonValue{false};
    }
    if (c == 'n' && text_.substr(at_, 4) == "null") {
      at_ += 4;
      return JsonValue{nullptr};
    }
    return JsonValue{readNumber()};
  }

  double readNumber() {
    const std::size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') ++at_;
    while (at_ < text_.size() && ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.' || text_[at_] == 'e' ||
                                  text_[at_] == 'E' || text_[at_] == '+' || text_[at_] == '-'))
      ++at_;
    if (at_ == start || at_ - start > 40) fail("bad number");
    const std::string token(text_.substr(start, at_ - start));
    char* end = nullptr;
    const double number = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(number)) fail("bad number");
    return number;
  }

  static void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }
  unsigned hex4() {
    if (at_ + 4 > text_.size()) fail("bad escape");
    unsigned code = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = text_[at_++];
      code <<= 4;
      if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
      else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
      else fail("bad escape");
    }
    return code;
  }
  std::string readString() {
    ++at_;  // the opening quote
    std::string out;
    for (;;) {
      if (at_ >= text_.size()) fail("unterminated string");
      const char c = text_[at_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      if (out.size() > limits_.stringBytes) fail("string too long");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at_ >= text_.size()) fail("bad escape");
      switch (const char e = text_[at_++]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = hex4();
          if (code >= 0xD800 && code < 0xDC00) {
            if (at_ + 2 > text_.size() || text_[at_] != '\\' || text_[at_ + 1] != 'u') fail("bad surrogate");
            at_ += 2;
            const unsigned low = hex4();
            if (low < 0xDC00 || low > 0xDFFF) fail("bad surrogate");
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            fail("bad surrogate");
          }
          appendUtf8(out, code);
          break;
        }
        default: fail(std::string("bad escape \\") + e);
      }
    }
  }
  JsonValue readArray(std::size_t depth) {
    ++at_;
    auto array = std::make_shared<JsonArray>();
    if (consume(']')) return JsonValue{array};
    for (;;) {
      array->push_back(readValue(depth + 1));
      if (consume(',')) continue;
      if (consume(']')) return JsonValue{array};
      fail("expected , or ]");
    }
  }
  JsonValue readObject(std::size_t depth) {
    ++at_;
    auto object = std::make_shared<JsonObject>();
    std::unordered_set<std::string> names;
    if (consume('}')) return JsonValue{object};
    for (;;) {
      skipSpace();
      if (at_ >= text_.size() || text_[at_] != '"') fail("expected a member name");
      std::string name = readString();
      if (!consume(':')) fail("expected :");
      if (!names.insert(name).second) fail("duplicate member " + name);
      object->emplace_back(std::move(name), readValue(depth + 1));
      if (consume(',')) continue;
      if (consume('}')) return JsonValue{object};
      fail("expected , or }");
    }
  }

  std::string_view text_;
  JsonLimits limits_;
  std::size_t at_ = 0;
  std::size_t values_ = 0;
};

}  // namespace sketchyup::ifc
