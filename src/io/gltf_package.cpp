#include "io/gltf_package.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cgltf.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
namespace sketchy {
namespace {
constexpr size_t fileLimit = 128 * 1024 * 1024, jsonLimit = 16 * 1024 * 1024;
constexpr size_t imageLimit = 16 * 1024 * 1024, imagesLimit = 64 * 1024 * 1024;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Budget {
    size_t used{}, peak{};
    struct alignas(std::max_align_t) Allocation {
        size_t size;
    };
    static void *allocate(void *user, cgltf_size size) {
        auto &budget = *static_cast<Budget *>(user);
        if (size > fileLimit - budget.used)
            return nullptr;
        auto *block = static_cast<Allocation *>(std::malloc(sizeof(Allocation) + size));
        if (!block)
            return nullptr;
        block->size = size;
        budget.used += size;
        budget.peak = std::max(budget.peak, budget.used);
        return block + 1;
    }
    static void release(void *user, void *pointer) {
        if (!pointer)
            return;
        auto &budget = *static_cast<Budget *>(user);
        auto *block = static_cast<Allocation *>(pointer) - 1;
        budget.used -= block->size;
        std::free(block);
    }
};
QByteArray readFile(const QString &path, size_t maximum) {
    QFile file(path);
    require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly) && !file.isSequential() &&
                file.size() >= 0 && quint64(file.size()) <= maximum,
            "Cannot read bounded glTF file");
    auto bytes = file.read(qint64(maximum) + 1);
    require(file.error() == QFileDevice::NoError && size_t(bytes.size()) <= maximum,
            "glTF file exceeds its byte limit");
    return bytes;
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
quint32 word(const QByteArray &bytes, qsizetype offset) {
    require(offset >= 0 && offset + 4 <= bytes.size(), "Truncated GLB header");
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
void range(size_t offset, size_t count, size_t stride, size_t element, size_t length) {
    require(count > 0 && count <= 1000000 && element > 0 && stride >= element && stride <= 256 &&
                offset <= length && element <= length - offset &&
                count - 1 <= (length - offset - element) / stride,
            "glTF accessor range exceeds its captured buffer view");
}
} // namespace
struct GltfPackage::Impl {
    Budget budget;
    QByteArray source;
    std::vector<QByteArray> buffers, images;
    cgltf_data *parsed{};
    QString parent;
    int sidecars{};
    ~Impl() {
        if (parsed)
            cgltf_free(parsed);
    }
    QByteArray uri(const char *raw, size_t maximum, bool image) {
        require(raw && std::strlen(raw) <= 24 * 1024 * 1024, "Missing or oversized glTF URI");
        const QByteArray uri(raw);
        if (uri.startsWith("data:")) {
            const auto comma = uri.indexOf(',');
            require(comma > 0, "Malformed glTF data URI");
            const auto media = uri.left(comma);
            require(image ? (media == "data:image/png;base64" || media == "data:image/jpeg;base64")
                          : (media == "data:application/octet-stream;base64" ||
                             media == "data:application/gltf-buffer;base64"),
                    "Unsupported glTF data URI media type");
            const auto encoded = uri.mid(comma + 1);
            require(size_t(encoded.size()) <= 4 * ((maximum + 2) / 3),
                    "glTF data URI exceeds bounds");
            const auto decoded =
                QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            require(decoded && decoded.decoded.toBase64() == encoded &&
                        size_t(decoded.decoded.size()) <= maximum,
                    "glTF data URI requires bounded canonical base64");
            return decoded.decoded;
        }
        require(uri.size() <= 4096, "glTF sidecar path exceeds bounds");
        for (qsizetype i = 0; i < uri.size(); ++i)
            if (uri[i] == '%') {
                auto hex = [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                           (c >= 'A' && c <= 'F');
                };
                require(i + 2 < uri.size() && hex(uri[i + 1]) && hex(uri[i + 2]),
                        "Invalid percent-encoded glTF path");
                i += 2;
            }
        const auto decoded = QUrl::fromPercentEncoding(uri);
        require(decoded.toUtf8() == QByteArray::fromPercentEncoding(uri),
                "glTF sidecar URI must contain valid UTF-8");
        require(!decoded.isEmpty() && decoded.isValidUtf16() && !decoded.contains(QChar(0)) &&
                    !decoded.contains(':') && !decoded.contains('\\') && !decoded.contains('?') &&
                    !decoded.contains('#') && !QDir::isAbsolutePath(decoded),
                "glTF sidecars must use relative local paths");
        const auto parts = decoded.split('/');
        require(!parts.contains(".."), "glTF sidecar escapes its source folder");
        const auto canonical = QFileInfo(QDir(parent).filePath(decoded)).canonicalFilePath();
        require(!canonical.isEmpty() &&
                    canonical.startsWith(parent.endsWith('/') ? parent : parent + '/'),
                "glTF sidecar must stay inside its source folder");
        ++sidecars;
        return readFile(canonical, maximum);
    }
    void open(const QString &path) {
        parent = QFileInfo(path).absoluteDir().canonicalPath();
        require(!parent.isEmpty(), "glTF source folder does not exist");
        source = readFile(path, fileLimit);
        QByteArray json = source;
        if (source.startsWith("glTF")) {
            require(word(source, 4) == 2 && word(source, 8) == size_t(source.size()),
                    "Unsupported or inconsistent GLB header");
            const auto length = word(source, 12);
            require(word(source, 16) == 0x4e4f534a && length <= jsonLimit && length % 4 == 0 &&
                        size_t(source.size()) >= 20 + size_t(length),
                    "Invalid GLB JSON chunk");
            json = source.mid(20, length);
            const auto end = 20 + qsizetype(length);
            if (end < source.size())
                require(word(source, end + 4) == 0x004e4942 && word(source, end) % 4 == 0 &&
                            size_t(source.size() - end - 8) == word(source, end),
                        "Unsupported GLB chunks or trailing data");
        }
        require(size_t(json.size()) <= jsonLimit, "glTF JSON exceeds 16 MiB");
        QJsonParseError error;
        const auto tree = QJsonDocument::fromJson(json, &error);
        require(error.error == QJsonParseError::NoError && tree.isObject() &&
                    tree.object()["asset"].toObject()["version"] == "2.0",
                "Expected glTF 2.0 JSON");
        const auto asset = tree.object()["asset"].toObject();
        require(!asset.contains("minVersion") || asset["minVersion"] == "2.0",
                "Unsupported minimum glTF version");
        // C strings cannot faithfully represent NUL in parsed names and URIs.
        std::vector<QJsonValue> pending{tree.object()};
        size_t visited{};
        while (!pending.empty()) {
            require(++visited <= 1000000, "glTF JSON exceeds its value budget");
            const auto value = pending.back();
            pending.pop_back();
            if (value.isString())
                require(!value.toString().contains(QChar(0)) && value.toString().isValidUtf16(),
                        "glTF strings require valid Unicode without NUL");
            else if (value.isArray()) {
                const auto array = value.toArray();
                require(size_t(array.size()) <= 1000000 - visited - pending.size(),
                        "glTF JSON exceeds its value budget");
                for (const auto &item : array)
                    pending.push_back(item);
            } else if (value.isObject()) {
                const auto object = value.toObject();
                require(size_t(object.size()) <= 1000000 - visited - pending.size(),
                        "glTF JSON exceeds its value budget");
                for (auto it = object.begin(); it != object.end(); ++it) {
                    require(!it.key().contains(QChar(0)), "glTF object keys cannot contain NUL");
                    pending.push_back(it.value());
                }
            }
        }
        cgltf_options options{};
        options.memory = {Budget::allocate, Budget::release, &budget};
        require(cgltf_parse(&options, source.constData(), size_t(source.size()), &parsed) ==
                    cgltf_result_success,
                "Cannot parse glTF within its resource bounds");
        require(parsed->nodes_count <= 10000 && parsed->meshes_count <= 10000 &&
                    parsed->materials_count <= 1024 && parsed->images_count <= 1024 &&
                    parsed->buffers_count <= 64 && parsed->buffer_views_count <= 65536 &&
                    parsed->accessors_count <= 65536 && parsed->scenes_count <= 256,
                "glTF record counts exceed limits");
        for (size_t i = 0; i < parsed->extensions_required_count; ++i)
            require(std::strcmp(parsed->extensions_required[i], "KHR_texture_transform") == 0,
                    "glTF requires an unsupported extension");
        size_t total{};
        buffers.reserve(parsed->buffers_count);
        for (size_t i = 0; i < parsed->buffers_count; ++i) {
            auto &buffer = parsed->buffers[i];
            require(buffer.size > 0 && buffer.size <= fileLimit - total,
                    "glTF buffers exceed 128 MiB");
            total += buffer.size;
            QByteArray bytes;
            if (buffer.uri)
                bytes = uri(buffer.uri, buffer.size, false);
            else {
                require(i == 0 && parsed->bin && parsed->bin_size >= buffer.size &&
                            parsed->bin_size - buffer.size <= 3,
                        "glTF buffer has no bounded embedded payload");
                bytes = QByteArray(static_cast<const char *>(parsed->bin), qsizetype(buffer.size));
            }
            require(size_t(bytes.size()) == buffer.size,
                    "glTF buffer length does not match its declaration");
            buffers.push_back(std::move(bytes));
            buffer.data = buffers.back().data();
            buffer.data_free_method = cgltf_data_free_method_none;
        }
        for (size_t i = 0; i < parsed->buffer_views_count; ++i) {
            const auto &view = parsed->buffer_views[i];
            require(view.buffer && view.size > 0 && !view.has_meshopt_compression &&
                        view.offset <= view.buffer->size &&
                        view.size <= view.buffer->size - view.offset && view.stride <= 256,
                    "Unsupported or out-of-range glTF buffer view");
        }
        size_t accessorElements{};
        for (size_t i = 0; i < parsed->accessors_count; ++i) {
            const auto &a = parsed->accessors[i];
            const auto component = cgltf_component_size(a.component_type),
                       element = cgltf_calc_size(a.type, a.component_type);
            require(component && element && a.count > 0 && a.count <= 1000000 &&
                        a.stride % component == 0,
                    "Invalid or oversized glTF accessor");
            require(a.count <= 2000000 - accessorElements,
                    "glTF accessor work exceeds its element budget");
            accessorElements += a.count;
            if (a.buffer_view) {
                require(a.offset % component == 0 && a.buffer_view->offset % component == 0,
                        "Unaligned glTF accessor");
                range(a.offset, a.count, a.stride, element, a.buffer_view->size);
            } else
                require(a.offset == 0, "Offset without glTF buffer view");
            if (a.is_sparse) {
                const auto &s = a.sparse;
                const auto size = cgltf_component_size(s.indices_component_type);
                require(s.count <= a.count && s.indices_buffer_view && s.values_buffer_view &&
                            (s.indices_component_type == cgltf_component_type_r_8u ||
                             s.indices_component_type == cgltf_component_type_r_16u ||
                             s.indices_component_type == cgltf_component_type_r_32u),
                        "Invalid sparse glTF accessor");
                // cgltf's scalar sparse readers advance values by accessor.stride.
                // Sparse payloads are tightly packed even when base data is interleaved;
                // reject that combination instead of reading beyond the captured values.
                require(a.stride == element && s.indices_buffer_view->stride == 0 &&
                            s.values_buffer_view->stride == 0,
                        "Sparse glTF accessors require tightly packed base and sparse views");
                require(s.indices_byte_offset % size == 0 &&
                            s.indices_buffer_view->offset % size == 0 &&
                            s.values_byte_offset % component == 0 &&
                            s.values_buffer_view->offset % component == 0,
                        "Unaligned sparse glTF accessor");
                range(s.indices_byte_offset, s.count, size, size, s.indices_buffer_view->size);
                range(s.values_byte_offset, s.count, element, element, s.values_buffer_view->size);
                const auto *indices =
                    static_cast<const char *>(s.indices_buffer_view->buffer->data) +
                    s.indices_buffer_view->offset + s.indices_byte_offset;
                size_t previous{};
                for (size_t j = 0; j < s.count; ++j) {
                    const auto index = size == 1 ? size_t(static_cast<unsigned char>(indices[j]))
                                       : size == 2
                                           ? size_t(qFromLittleEndian<quint16>(indices + 2 * j))
                                           : size_t(qFromLittleEndian<quint32>(indices + 4 * j));
                    require(index < a.count && (j == 0 || index > previous),
                            "Sparse glTF indices must be unique, ordered and in range");
                    previous = index;
                }
            }
        }
        size_t primitiveElements{}, primitives{};
        for (size_t i = 0; i < parsed->meshes_count; ++i) {
            const auto &mesh = parsed->meshes[i];
            require(mesh.primitives_count <= 10000 - primitives,
                    "glTF primitive count exceeds limits");
            primitives += mesh.primitives_count;
            for (size_t j = 0; j < mesh.primitives_count; ++j) {
                const auto &primitive = mesh.primitives[j];
                require(primitive.attributes_count > 0 && primitive.attributes_count <= 16,
                        "glTF primitive attribute count exceeds limits");
                const auto count = primitive.indices ? primitive.indices->count
                                                     : primitive.attributes[0].data->count;
                require(count <= 1000000 - primitiveElements,
                        "glTF primitive validation exceeds its work budget");
                primitiveElements += count;
            }
        }
        for (size_t i = 0; i < parsed->nodes_count; ++i) {
            size_t depth{};
            for (auto node = &parsed->nodes[i]; node; node = node->parent)
                require(++depth <= 256, "glTF hierarchy is cyclic or exceeds depth 256");
        }
        require(cgltf_validate(parsed) == cgltf_result_success,
                "glTF buffer, topology or scene validation failed");
        total = 0;
        images.reserve(parsed->images_count);
        for (size_t i = 0; i < parsed->images_count; ++i) {
            const auto &image = parsed->images[i];
            require(bool(image.uri) != bool(image.buffer_view),
                    "glTF image requires exactly one source");
            require(!image.mime_type || std::strcmp(image.mime_type, "image/png") == 0 ||
                        std::strcmp(image.mime_type, "image/jpeg") == 0,
                    "Only PNG and JPEG glTF images are supported");
            require(!image.buffer_view || image.mime_type,
                    "Embedded glTF image requires a MIME type");
            QByteArray bytes;
            if (image.uri)
                bytes = uri(image.uri, imageLimit, true);
            else {
                const auto &view = *image.buffer_view;
                require(view.size <= imageLimit && view.buffer->data,
                        "glTF image exceeds its byte limit");
                bytes = QByteArray(static_cast<const char *>(view.buffer->data) + view.offset,
                                   qsizetype(view.size));
            }
            require(size_t(bytes.size()) <= imagesLimit - total, "glTF images exceed 64 MiB");
            total += size_t(bytes.size());
            images.push_back(std::move(bytes));
        }
    }
};
GltfPackage::GltfPackage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GltfPackage::~GltfPackage() = default;
GltfPackage::GltfPackage(GltfPackage &&) noexcept = default;
GltfPackage &GltfPackage::operator=(GltfPackage &&) noexcept = default;
GltfPackage GltfPackage::read(const QString &path) {
    auto impl = std::make_unique<Impl>();
    impl->open(path);
    return GltfPackage(std::move(impl));
}
const cgltf_data &GltfPackage::data() const { return *impl_->parsed; }
const QByteArray &GltfPackage::image(size_t index) const { return impl_->images.at(index); }
QJsonObject GltfPackage::report() const {
    QJsonArray buffers, images;
    for (const auto &bytes : impl_->buffers)
        buffers.append(QJsonObject{{"bytes", bytes.size()}, {"sha256", hash(bytes)}});
    for (const auto &bytes : impl_->images)
        images.append(QJsonObject{{"bytes", bytes.size()}, {"sha256", hash(bytes)}});
    return {{"apiVersion", 1},
            {"format", impl_->parsed->file_type == cgltf_file_type_glb ? "glb" : "gltf"},
            {"sourceSha256", hash(impl_->source)},
            {"sourceBytes", impl_->source.size()},
            {"buffers", buffers},
            {"images", images},
            {"sidecars", impl_->sidecars},
            {"parserPeakBytes", double(impl_->budget.peak)}};
}
} // namespace sketchy
