#include "kachakacha/app/EntityNaming.h"

namespace kachakacha::v2::app {
namespace {

//! 半角・全角の空白と改行。全角空白を落とさないと、
//! 日本語入力のまま打った名前が「見えない文字入り」になる。
[[nodiscard]] bool IsBlankAt(const std::string& text, std::size_t index,
    std::size_t& widthOut)
{
    const unsigned char first = static_cast<unsigned char>(text[index]);
    if (first == ' ' || first == '\t' || first == '\n' || first == '\r') {
        widthOut = 1;
        return true;
    }
    // U+3000 IDEOGRAPHIC SPACE = E3 80 80
    if (first == 0xE3 && index + 2 < text.size()
        && static_cast<unsigned char>(text[index + 1]) == 0x80
        && static_cast<unsigned char>(text[index + 2]) == 0x80) {
        widthOut = 3;
        return true;
    }
    return false;
}

} // namespace

base::Result<std::string> NormalizeEntityName(const std::string& input)
{
    std::size_t begin = 0;
    std::size_t width = 0;
    while (begin < input.size() && IsBlankAt(input, begin, width)) {
        begin += width;
    }
    std::size_t end = input.size();
    while (end > begin) {
        // 末尾から見るので、1バイトぶんずつ戻して、そこが空白の頭かを見る。
        std::size_t candidate = end >= 3 ? end - 3 : 0;
        bool trimmed = false;
        for (std::size_t at = candidate; at < end; ++at) {
            if (at >= begin && IsBlankAt(input, at, width) && at + width == end) {
                end = at;
                trimmed = true;
                break;
            }
        }
        if (!trimmed) {
            break;
        }
    }
    std::string name = input.substr(begin, end - begin);
    if (name.empty()) {
        return base::Result<std::string>::Failure({base::MakeError("UI-N001",
            "名前が空です。", "空白だけの名前は付けられません。")});
    }
    return base::Result<std::string>::Success(std::move(name));
}

bool NameActuallyChanges(const std::string& current, const std::string& normalized) noexcept
{
    return current != normalized;
}

} // namespace kachakacha::v2::app
