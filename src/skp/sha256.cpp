#include "sha256.hpp"

namespace sketchyup::skp {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0xf4053625, 0x20f4e2c1,
    0x3e021e65, 0x2893beac, 0x5493c784, 0x162ab678, 0x843380de, 0x98095432, 0x9b00aed0, 0xf83e53c3,
    0x94d04bad, 0x4fde4b28, 0xb688a020, 0x01a9a026, 0x55275182, 0x713c12b9, 0x2323c73e, 0xbcd60781,
    0xf9a2f71c, 0x6579a540, 0x5c0dcafa, 0x43b37779, 0x05e5626a, 0x28f1c7d7, 0x0e02bacc, 0x074c5d99,
    0x4e0c0960, 0x9c67b9c9, 0xa7e64d9b, 0x673fb544, 0x2fd1be00, 0x34186b4a, 0x5259b956, 0xf1d000a4,
    0x606d8f21, 0x033e5a19, 0x7817fa19, 0x2f3b2b0a, 0x3763c253, 0xa5a3207b, 0xd1925431, 0x7112a94e,
    0x298b1f16, 0xa2c1b0e4, 0x6b0528f7, 0x5a1a4e65, 0x3a414129, 0x3b056d83, 0x10897d22, 0x3a161f78};

std::uint32_t rotr(std::uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32 - bits));
}

}  // namespace

void Sha256::update(const std::uint8_t* data, std::size_t bytes) {
  totalBytes_ += bytes;
  buffer_.insert(buffer_.end(), data, data + bytes);
  while (buffer_.size() >= 64) {
    block(buffer_.data());
    buffer_.erase(buffer_.begin(), buffer_.begin() + 64);
  }
}

void Sha256::block(const std::uint8_t* data) {
  std::uint32_t w[64];
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = (std::uint32_t(data[i * 4]) << 24) | (std::uint32_t(data[i * 4 + 1]) << 16) |
           (std::uint32_t(data[i * 4 + 2]) << 8) | std::uint32_t(data[i * 4 + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t bigI = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t chi = (e & f) ^ (~e & g);
    const std::uint32_t e1 = h + bigI + chi + kRoundConstants[i] + w[i];
    const std::uint32_t bigK = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t sum = bigK + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + e1;
    d = c;
    c = b;
    b = a;
    a = e1 + sum;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::finalise() {
  if (finalised_) return;
  finalised_ = true;
  const std::uint64_t bits = totalBytes_ * 8;
  buffer_.push_back(0x80);
  while (buffer_.size() % 64 != 56) buffer_.push_back(0);
  for (int i = 7; i >= 0; --i) buffer_.push_back(std::uint8_t((bits >> (i * 8)) & 0xFF));
  while (buffer_.size() >= 64) {
    block(buffer_.data());
    buffer_.erase(buffer_.begin(), buffer_.begin() + 64);
  }
}

std::string Sha256::hex() {
  finalise();
  std::string out;
  out.reserve(64);
  for (const std::uint32_t word : state_) {
    char buffer[9];
    std::snprintf(buffer, sizeof buffer, "%08x", word);
    out.append(buffer);
  }
  return out;
}

std::string sha256_hex(const std::uint8_t* data, std::size_t bytes) {
  Sha256 digest;
  digest.update(data, bytes);
  return digest.hex();
}

}  // namespace sketchyup::skp
