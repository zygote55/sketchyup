// A small SHA-256 (FIPS 180-4) for the helper's package checksums: the source SHA-256 in the
// manifest and the geometry / texture chunk digests the decoder verifies. The helper has no Qt
// (ADR 0176 decision 2), so it does not take the digest from QCryptographicHash.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sketchyup::skp {

class Sha256 {
public:
  void update(const std::uint8_t* data, std::size_t bytes);
  void update(const std::string& text) {
    update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  }
  std::string hex();  // lowercase, 64 characters

private:
  void block(const std::uint8_t* data);
  void finalise();

  // The standard initial hash values.
  std::array<std::uint32_t, 8> state_{
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<std::uint8_t> buffer_;
  std::uint64_t totalBytes_{};
  bool finalised_ = false;
};

// One-shot helper.
std::string sha256_hex(const std::uint8_t* data, std::size_t bytes);

}  // namespace sketchyup::skp
