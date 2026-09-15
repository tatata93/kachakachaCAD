#include "kachakacha/app/ExtrudePreview.h"

namespace kachakacha::v2::app {

PreviewFaces ExtrudeSweptFaces(const std::vector<Vector3>& outline, const Vector3& offset)
{
    PreviewFaces faces;
    if (outline.size() < 2 || offset.Length() < 1.0e-9) {
        return faces;   // 厚みが無い。厚く見せない。
    }
    for (std::size_t index = 0; index + 1 < outline.size(); ++index) {
        const Vector3& a = outline[index];
        const Vector3& b = outline[index + 1];
        faces.push_back({a, b, b + offset, a + offset});
    }
    // 押し出し先のふた。どこまで進んだかが、線だけより読みやすくなる。
    std::vector<Vector3> cap;
    cap.reserve(outline.size());
    for (const Vector3& point : outline) {
        cap.push_back(point + offset);
    }
    faces.push_back(std::move(cap));
    return faces;
}

} // namespace kachakacha::v2::app
