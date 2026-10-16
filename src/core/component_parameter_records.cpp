#include "core/component_parameter_records.hpp"
#include <bit>
#include <cstring>
namespace sketchy {
namespace {
std::string utf8Cut(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes)
        return std::string(text);
    while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80)
        --bytes;
    return std::string(text.substr(0, bytes));
}
} // namespace
std::string boundedParameterMessage(std::string_view message) { return utf8Cut(message, 256); }
std::string quotedParameterText(std::string_view text) {
    std::string clean;
    clean.reserve(std::min<std::size_t>(text.size(), 80));
    for (const char c : text.substr(0, 80))
        clean += static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? '?' : c;
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    if (text.size() > 64)
        clean = utf8Cut(clean, 64 - ellipsis.size()) + std::string(ellipsis);
    return "'" + clean + "'";
}
ComponentParameterError::ComponentParameterError(std::string code, const std::string &message,
                                                 ComponentParameterErrorContext context)
    : std::runtime_error(boundedParameterMessage(message)), code_(std::move(code)),
      context_(std::move(context)) {}
ComponentParameterError ComponentParameterError::withPlacement(Id placement) const {
    if (context_.placement || !placement)
        return *this;
    auto context = context_;
    context.placement = placement;
    std::string message = what();
    const auto suffix = " (placement #" + std::to_string(placement) + ")";
    if (message.size() + suffix.size() <= 256)
        message += suffix;
    return ComponentParameterError(code_, message, std::move(context));
}
void throwParameterExpressionError(const ParameterExpressionError &error,
                                   ComponentParameterErrorContext context) {
    context.offset = error.offset();
    context.excerpt = error.excerpt();
    std::string subject;
    if (!context.parameter.empty())
        subject = "parameter " + quotedParameterText(context.parameter);
    else if (context.driver)
        subject = "driver #" + std::to_string(context.driver);
    std::string message = error.what();
    if (!subject.empty())
        message = subject + " of definition #" + std::to_string(context.definition) + ": " +
                  message;
    throw ComponentParameterError(std::string(parameterErrorCodeName(error.code())), message,
                                  std::move(context));
}
std::string_view componentDriverKindName(ComponentDriverKind kind) {
    switch (kind) {
    case ComponentDriverKind::Stretch:
        return "stretch";
    case ComponentDriverKind::Position:
        return "position";
    case ComponentDriverKind::Rotation:
        return "rotation";
    case ComponentDriverKind::Visibility:
        return "visibility";
    case ComponentDriverKind::Repeat:
        return "repeat";
    case ComponentDriverKind::ChildParameter:
        return "childParameter";
    }
    return "unknown";
}
std::string_view componentAxisName(ComponentAxis axis) {
    switch (axis) {
    case ComponentAxis::X:
        return "x";
    case ComponentAxis::Y:
        return "y";
    case ComponentAxis::Z:
        return "z";
    }
    return "unknown";
}
Vec3 componentAxisVector(ComponentAxis axis) {
    switch (axis) {
    case ComponentAxis::X:
        return {1, 0, 0};
    case ComponentAxis::Y:
        return {0, 1, 0};
    case ComponentAxis::Z:
        return {0, 0, 1};
    }
    throw std::runtime_error("Unknown component axis");
}
std::size_t componentParametersBytes(const ComponentParametersPtr &record) {
    if (!record)
        return 0;
    std::size_t bytes = sizeof(ComponentParameters) + 64;
    for (const auto &parameter : record->parameters) {
        bytes += sizeof(ComponentParameter) + 64 + parameter.name.size() +
                 parameter.label.size() + parameter.expression.size();
        for (const auto &option : parameter.options)
            bytes += sizeof(ComponentParameterOption) + 32 + option.value.size() +
                     option.label.size();
    }
    for (const auto &driver : record->drivers)
        bytes += sizeof(ComponentDriver) + 64 + driver.expression.size() + driver.spacing.size() +
                 driver.parameter.size();
    if (record->assembly)
        bytes += sizeof(ComponentAssembly) + record->assembly->id.size();
    return bytes;
}
std::size_t componentOverridesBytes(const ComponentOverrides &overrides) {
    std::size_t bytes = 0;
    for (const auto &[name, value] : overrides) {
        bytes += 96 + name.size() + sizeof(ParameterValue);
        if (const auto *choice = std::get_if<ParameterChoice>(&value))
            bytes += choice->value.size();
    }
    return bytes;
}

// ---- Canonical encoding ----
namespace {
void u8(std::string &out, std::uint8_t value) { out.push_back(static_cast<char>(value)); }
void u64(std::string &out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}
void f64(std::string &out, double value) { u64(out, std::bit_cast<std::uint64_t>(value)); }
void f32(std::string &out, float value) { u64(out, std::bit_cast<std::uint32_t>(value)); }
void text(std::string &out, std::string_view value) {
    u64(out, value.size());
    out.append(value);
}
void vec(std::string &out, Vec3 value) {
    f64(out, value.x);
    f64(out, value.y);
    f64(out, value.z);
}
void optional(std::string &out, const std::optional<double> &value) {
    u8(out, value ? 1 : 0);
    if (value)
        f64(out, *value);
}
void color(std::string &out, const std::array<float, 3> &value) {
    for (const auto component : value)
        f32(out, component);
}
void mapping(std::string &out, const std::optional<TextureMapping> &value) {
    u8(out, value ? 1 : 0);
    if (!value)
        return;
    vec(out, value->origin);
    vec(out, value->uGradient);
    vec(out, value->vGradient);
    f64(out, value->offset.u);
    f64(out, value->offset.v);
}
void transform(std::string &out, const Transform &value) {
    for (const auto entry : value.m)
        f64(out, entry);
}
} // namespace
void appendCanonicalBody(std::string &out, const Body &body) {
    out.append("body");
    u64(out, body.id);
    text(out, body.name);
    color(out, body.color);
    u64(out, body.surface.vertices.size());
    for (const auto &[id, point] : body.surface.vertices) {
        u64(out, id);
        vec(out, point);
    }
    u64(out, body.surface.faces.size());
    for (const auto &[id, face] : body.surface.faces) {
        u64(out, id);
        u64(out, face.loops.size());
        for (const auto &loop : face.loops) {
            u64(out, loop.size());
            for (const auto vertex : loop)
                u64(out, vertex);
        }
    }
    u64(out, body.surface.wires.size());
    for (const auto &wire : body.surface.wires) {
        u64(out, wire[0]);
        u64(out, wire[1]);
    }
    u64(out, body.topology.edges.size());
    for (const auto &[id, edge] : body.topology.edges) {
        u64(out, id);
        u64(out, edge.a);
        u64(out, edge.b);
        u8(out, edge.wire);
    }
    u64(out, body.edgeAppearances.size());
    for (const auto &[id, appearance] : body.edgeAppearances) {
        u64(out, id);
        u8(out, appearance.hidden | appearance.soft << 1 | appearance.smooth << 2);
    }
    u64(out, body.faceColors.size());
    for (const auto &[id, value] : body.faceColors) {
        u64(out, id);
        color(out, value);
    }
    u64(out, body.materials.front);
    u64(out, body.materials.back);
    u64(out, body.faceMaterials.size());
    for (const auto &[id, sides] : body.faceMaterials) {
        u64(out, id);
        u64(out, sides.front);
        u64(out, sides.back);
    }
    u64(out, body.faceTextureMappings.size());
    for (const auto &[id, sides] : body.faceTextureMappings) {
        u64(out, id);
        mapping(out, sides.front);
        mapping(out, sides.back);
    }
    u64(out, body.curves.size());
    for (const auto &[id, curve] : body.curves) {
        u64(out, id);
        u8(out, static_cast<std::uint8_t>(curve.kind));
        vec(out, curve.center);
        vec(out, curve.xAxis);
        vec(out, curve.yAxis);
        f64(out, curve.radius);
        f64(out, curve.startAngle);
        f64(out, curve.sweepAngle);
        u64(out, curve.segments);
        u64(out, curve.edges.size());
        for (const auto &edge : curve.edges) {
            u64(out, edge.edge);
            u8(out, edge.reversed);
        }
    }
    u64(out, body.guides.size());
    for (const auto &[id, guide] : body.guides) {
        u64(out, id);
        u8(out, static_cast<std::uint8_t>(guide.kind));
        vec(out, guide.origin);
        vec(out, guide.direction);
    }
    transform(out, body.transform);
    u64(out, body.parent);
    u8(out, static_cast<std::uint8_t>(body.kind));
    u8(out, body.hidden | body.locked << 1);
    u64(out, body.tag);
    u64(out, body.properties.size());
    for (const auto &[key, value] : body.properties) {
        text(out, key);
        u8(out, static_cast<std::uint8_t>(value.index()));
        if (const auto *flag = std::get_if<bool>(&value))
            u8(out, *flag);
        else if (const auto *number = std::get_if<double>(&value))
            f64(out, *number);
        else
            text(out, std::get<std::string>(value));
    }
    u8(out, body.textSource ? 1 : 0);
    if (body.textSource) {
        const auto &source = *body.textSource;
        text(out, source.text);
        text(out, source.family);
        text(out, source.style);
        f64(out, source.height);
        f64(out, source.depth);
        f64(out, source.lineSpacing);
        u8(out, source.allowSubstitution | source.substituted << 1 | source.fallback << 2);
        text(out, source.actualFamily);
        text(out, source.actualStyle);
        u64(out, source.fonts.size());
        for (const auto &font : source.fonts) {
            text(out, font.family);
            text(out, font.style);
            text(out, font.fingerprint);
            u64(out, font.glyphs);
        }
        text(out, source.geometryDigest);
        u64(out, source.regions);
    }
    u8(out, body.referenceImage ? 1 : 0);
    if (body.referenceImage) {
        u64(out, body.referenceImage->asset);
        f64(out, body.referenceImage->width);
        f64(out, body.referenceImage->height);
        f64(out, body.referenceImage->opacity);
    }
}
void appendCanonicalValue(std::string &out, const ParameterValue &value) {
    u8(out, static_cast<std::uint8_t>(value.index()));
    if (const auto *flag = std::get_if<bool>(&value))
        u8(out, *flag);
    else if (const auto *choice = std::get_if<ParameterChoice>(&value))
        text(out, choice->value);
    else {
        const auto &quantity = std::get<ParameterQuantity>(value);
        f64(out, quantity.value);
        u8(out, static_cast<std::uint8_t>(quantity.dimension.length + 8));
        u8(out, static_cast<std::uint8_t>(quantity.dimension.angle + 8));
    }
}
void appendCanonicalOverrides(std::string &out, const ComponentOverrides &overrides) {
    u64(out, overrides.size());
    for (const auto &[name, value] : overrides) {
        text(out, name);
        appendCanonicalValue(out, value);
    }
}
void appendCanonicalParameterList(std::string &out, const std::vector<ComponentParameter> &list) {
    out.append("parameters");
    u64(out, list.size());
    for (const auto &parameter : list) {
        text(out, parameter.name);
        text(out, parameter.label);
        u8(out, static_cast<std::uint8_t>(parameter.type));
        u8(out, static_cast<std::uint8_t>(parameter.mode));
        text(out, parameter.expression);
        optional(out, parameter.min);
        optional(out, parameter.max);
        optional(out, parameter.step);
        u64(out, parameter.options.size());
        for (const auto &option : parameter.options) {
            text(out, option.value);
            text(out, option.label);
        }
    }
}
void appendCanonicalParameters(std::string &out, const ComponentParameters &record) {
    appendCanonicalParameterList(out, record.parameters);
    out.append("drivers");
    u64(out, record.drivers.size());
    for (const auto &driver : record.drivers) {
        u64(out, driver.id);
        u8(out, static_cast<std::uint8_t>(driver.kind));
        u64(out, driver.member);
        text(out, driver.expression);
        u8(out, static_cast<std::uint8_t>(driver.axis));
        f64(out, driver.at);
        u64(out, driver.maxCount);
        text(out, driver.spacing);
        u64(out, driver.firstMember);
        text(out, driver.parameter);
        out.append(reinterpret_cast<const char *>(driver.bound.data()), driver.bound.size());
    }
    // Appended only when present, so records without one keep their encoding.
    if (record.assembly) {
        out.append("assembly");
        text(out, record.assembly->id);
        u64(out, record.assembly->version);
    }
}
bool validComponentAssembly(std::string_view id, double version) {
    if (!(version >= 1 && version <= componentAssemblyMaxVersion) ||
        version != static_cast<double>(static_cast<std::uint32_t>(version)))
        return false;
    auto segment = [](std::string_view part) {
        if (part.empty() || part.size() > 64)
            return false;
        for (const char c : part)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
        return true;
    };
    const auto slash = id.find('/');
    if (slash == std::string_view::npos)
        return segment(id);
    return segment(id.substr(0, slash)) && segment(id.substr(slash + 1));
}

// ---- SHA-256 (FIPS 180-4) ----
namespace {
constexpr std::array<std::uint32_t, 64> roundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};
std::uint32_t rotr(std::uint32_t value, int bits) { return std::rotr(value, bits); }
void block(std::array<std::uint32_t, 8> &state, const unsigned char *data) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i)
        w[i] = std::uint32_t(data[4 * i]) << 24 | std::uint32_t(data[4 * i + 1]) << 16 |
               std::uint32_t(data[4 * i + 2]) << 8 | std::uint32_t(data[4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (int i = 0; i < 64; ++i) {
        const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + choose + roundConstants[i] + w[i];
        const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}
} // namespace
ComponentDigest sha256Digest(std::string_view bytes) {
    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
    std::size_t offset = 0;
    for (; offset + 64 <= bytes.size(); offset += 64)
        block(state, data + offset);
    std::array<unsigned char, 128> tail{};
    const auto remaining = bytes.size() - offset;
    if (remaining)
        std::memcpy(tail.data(), data + offset, remaining);
    tail[remaining] = 0x80;
    const auto blocks = remaining + 9 <= 64 ? 1 : 2;
    const std::uint64_t bits = std::uint64_t(bytes.size()) * 8;
    for (int i = 0; i < 8; ++i)
        tail[blocks * 64 - 1 - i] = static_cast<unsigned char>(bits >> (8 * i));
    for (int i = 0; i < blocks; ++i)
        block(state, tail.data() + 64 * i);
    ComponentDigest digest{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            digest[4 * i + j] = static_cast<std::uint8_t>(state[i] >> (24 - 8 * j));
    return digest;
}
std::string digestHex(const ComponentDigest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const auto byte : digest) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}
} // namespace sketchy
