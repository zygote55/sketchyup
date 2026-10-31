#pragma once
#include "core/camera_motion.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <vector>
namespace sketchy {
// The request file of --export-presentation (ADR 0173 §4, §7): at most 16 KiB of JSON. It names
// one camera path by name, or an explicit list of saved scenes with timing, then one to four
// render presets by name, the new output folder, the optional video flag and the optional frame
// credit flag.
inline constexpr int presentationRequestBytes = 16 * 1024;
struct PresentationRequest {
    QString path;                   // A camera path by name; empty with an explicit list.
    QString name;                   // Names an explicit list; the default is "Keyframes".
    std::vector<QString> keyframes; // Saved scene names in playback order, for an explicit list.
    CameraPathTiming timing;        // Only an explicit list carries timing in the request.
    bool explicitKeyframes{};
    std::vector<QString> presets;
    QString output;
    bool video{}; // Encode each preset to video.mp4 (decision 7). Off unless requested.
    // The video's codec, "h264" (the default) or "mpeg4" (MPEG-4 Part 2, the explicit fallback of
    // UX_DESIGN 17.12). It is named only with video true, and it is never switched for another.
    QString videoCodec{"h264"};
    // Burn the notices of the DEM terrain in each frame's view into the frame (UX_DESIGN 17.13). On
    // by default; the manifest lists the notices either way.
    bool burnCredits{true};
};
// Refuses unknown fields, both or neither path forms, a wrong apiVersion, a non-object, a
// preset list outside one to four, a duplicate preset and a non-boolean video flag. Other limits
// are named by the batch engine when it resolves the request.
PresentationRequest parsePresentationRequest(const QByteArray &bytes);
QJsonObject presentationExportCapabilities();
QJsonObject presentationExportDescription();
} // namespace sketchy
