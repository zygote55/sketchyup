#include "app/viewport.hpp"
#include "core/face_textures.hpp"

namespace sketchy {
void Viewport::syncTextures() {
    AssetRecords assets;
    // Even an empty request must retire obsolete CPU and GPU images below.
    if (!doc_.assets().empty()) {
        // The request is the set of assets drawn by visible faces and reference
        // images. Visibility walks the hierarchy, so it is tested only for an
        // entity that would add an asset not already requested.
        std::vector<Id> added;
        auto unrequested = [&](MaterialSides sides) {
            added.clear();
            for (auto material : {sides.front, sides.back})
                if (material)
                    if (const auto asset = doc_.materials().at(material)->asset;
                        asset && !assets.contains(asset))
                        added.push_back(asset);
            return !added.empty();
        };
        auto request = [&] {
            for (auto asset : added)
                assets.emplace(asset, doc_.assets().at(asset));
        };
        for (const auto &[id, body] : doc_.bodies()) {
            if (opacity_.contains(id) && opacity_.at(id) == 0)
                continue;
            if (body->referenceImage && !assets.contains(body->referenceImage->asset) &&
                visible({id, SelectionKind::Body, 0}))
                assets.emplace(body->referenceImage->asset,
                               doc_.assets().at(body->referenceImage->asset));
            // Without per-face assignments every face draws the body's materials.
            if (body->faceMaterials.empty() && !unrequested(body->materials))
                continue;
            for (const auto &[face, record] : body->surface.faces) {
                if (!unrequested(faceMaterials(*body, face)) ||
                    !visible({id, SelectionKind::Face, face}))
                    continue;
                request();
            }
        }
    }
    // The edge texture (R100.f1) is decoded while profiles draw it. A missing one has no bytes to
    // decode and is drawn hatched instead.
    if (doc_.style().profiles && doc_.style().edgeStroke && doc_.style().edgeStroke->texture) {
        const auto texture = doc_.style().edgeStroke->texture;
        if (const auto found = doc_.assets().find(texture);
            found != doc_.assets().end() && found->second && found->second->payload)
            assets.emplace(texture, found->second);
    }
    textureCache_.request(assets);
    const auto published = textureCache_.snapshot();
    if (published != textureSnapshot_)
        cacheDirty_ = pickDirty_ = true;
    textureSnapshot_ = published;
    std::map<Id, std::shared_ptr<const TextureImage>> images;
    for (const auto &[id, asset] : assets)
        if (const auto *entry = TextureCache::find(*textureSnapshot_, asset); entry && entry->image)
            images.emplace(id, entry->image);
    // Release obsolete GPU storage before allocating replacements. Published CPU
    // images already obey the cache's decoded budget and image count (R118).
    std::erase_if(textureGpu_, [&](const auto &item) {
        const auto [id, name] = item;
        if (images.contains(id) && textureImages_.contains(id) &&
            images.at(id) == textureImages_.at(id))
            return false;
        gl_->glDeleteTextures(1, &name);
        return true;
    });
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto it = images.begin(); it != images.end();) {
        const auto &[id, image] = *it;
        if (!textureGpu_.contains(id)) {
            GLuint name{};
            gl_->glGenTextures(1, &name);
            gl_->glBindTexture(GL_TEXTURE_2D, name);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image->width(), image->height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, image->rgba().data());
            if (!name || gl_->glGetError() != GL_NO_ERROR) {
                gl_->glDeleteTextures(1, &name);
                it = images.erase(it);
                continue;
            }
            textureGpu_.emplace(id, name);
        }
        ++it;
    }
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    textureAssets_ = std::move(assets);
    textureImages_ = std::move(images);
    textureStored_.clear();
    textureUnavailable_ = textureReduced_ = 0;
    for (const auto &[id, asset] : textureAssets_) {
        const auto *entry = TextureCache::find(*textureSnapshot_, asset);
        if (!textureImages_.contains(id)) {
            // A budget refusal has its own message. Any other image without pixels is unavailable.
            if (!entry || entry->status != "budget")
                ++textureUnavailable_;
            continue;
        }
        textureStored_.emplace(id, std::pair{entry->storedWidth, entry->storedHeight});
        if (entry->reduced)
            ++textureReduced_;
    }
    textureFallbacks_ = textureAssets_.size() - textureImages_.size();
}
Viewport::TextureProjection Viewport::textureProjection(const Body &body, const Triangle &local,
                                                        bool back) const {
    if (body.referenceImage) {
        const auto &reference = *body.referenceImage;
        if (!textureImages_.contains(reference.asset) ||
            !textureAssets_.contains(reference.asset) ||
            !TextureCache::sameImage(textureAssets_.at(reference.asset),
                                     doc_.assets().at(reference.asset)))
            return {};
        TextureProjection result;
        result.image = reference.asset;
        size_t i = 0;
        for (auto p : {local.a, local.b, local.c})
            result.uv[i++] = {float(p.x / reference.width), float(1 - p.y / reference.height)};
        return result;
    }
    const auto sides = faceMaterials(body, local.face);
    const auto material = back ? sides.back : sides.front;
    if (!material)
        return {};
    const auto asset = doc_.materials().at(material)->asset;
    if (!asset || !textureImages_.contains(asset) || !textureAssets_.contains(asset) ||
        !TextureCache::sameImage(textureAssets_.at(asset), doc_.assets().at(asset)))
        return {};
    try {
        const auto mapping = effectiveFaceTextureMapping(body, local.face, back, doc_.materials());
        // The guard uses the stored size, so a reduced image draws the same projections as the full one.
        const auto [storedWidth, storedHeight] = textureStored_.at(asset);
        const auto uv =
            floatTextureCoordinates({mapping.coordinates(local.a), mapping.coordinates(local.b),
                                     mapping.coordinates(local.c)},
                                    {1. / (64 * storedWidth), 1. / (64 * storedHeight)});
        return {asset, uv};
    } catch (const std::exception &) {
        // An extreme projection must not disable the viewport. Picking uses
        // this same precision guard and therefore the same swatch fallback.
        return {};
    }
}
void Viewport::textureVertices(const Body &body, const Triangle &local, bool reflected,
                               std::array<Vertex, 3> &vertices) const {
    const auto front = textureProjection(body, local, reflected);
    const auto back = textureProjection(body, local, !reflected);
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto &v = vertices[i];
        v.image = front.image;
        v.backImage = back.image;
        v.u = front.uv[i][0];
        v.v = front.uv[i][1];
        v.bu = back.uv[i][0];
        v.bv = back.uv[i][1];
    }
}
QString Viewport::textureSummary() const {
    if (textureCache_.pending())
        return "Loading images…";
    if (textureMappingFallbacks_)
        return "Some textures use a color preview: mapping too large";
    if (textureUnavailable_)
        return "Some images are unavailable; reference planes show a purple placeholder";
    // Even the smallest sizes do not fit the budget (R118).
    if (textureFallbacks_)
        return "Some images do not fit the viewport's memory budget, even at reduced resolution; "
               "reference planes show a purple placeholder";
    if (textureReduced_)
        return "Some textures are shown at reduced resolution to fit the viewport's memory budget";
    return {};
}
std::vector<std::pair<QSize, QSize>> Viewport::textureSizes() const {
    std::vector<std::pair<QSize, QSize>> sizes;
    for (const auto &requested : textureAssets_) {
        const auto image = textureImages_.find(requested.first);
        const auto stored = textureStored_.find(requested.first);
        sizes.emplace_back(
            image == textureImages_.end() ? QSize()
                                          : QSize(image->second->width(), image->second->height()),
            stored == textureStored_.end() ? QSize()
                                           : QSize(stored->second.first, stored->second.second));
    }
    return sizes;
}
} // namespace sketchy
