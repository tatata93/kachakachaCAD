#include "kachakacha/app/DocumentIds.h"

#include "kachakacha/base/Uuid.h"
#include "kachakacha/io/DocumentFile.h"

#include <algorithm>
#include <cctype>

namespace kachakacha::v2::app {

std::uint64_t MaxDeterministicCounterIn(std::string_view text)
{
    // 36 文字の小文字ハイフン付き UUID を全部見て、決定的なものだけ番号を読む。
    constexpr std::size_t kLength = 36;
    std::uint64_t best = 0;
    const auto isHex = [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    };
    for (std::size_t at = 0; at + kLength <= text.size(); ++at) {
        if (at > 0 && isHex(text[at - 1])) {
            continue;   // 途中から読まない
        }
        const std::string_view candidate = text.substr(at, kLength);
        if (candidate[8] != '-' || candidate[13] != '-' || candidate[18] != '-'
            || candidate[23] != '-') {
            continue;
        }
        const auto parsed = base::Uuid::Parse(candidate);
        if (!parsed.has_value()) {
            continue;
        }
        const auto counter = base::DeterministicIdGenerator::CounterOf(*parsed);
        if (counter.has_value()) {
            best = std::max(best, *counter);
        }
    }
    return best;
}

std::uint64_t AdvanceIdsPastDocument(base::IdGenerator& ids,
    const document::DocumentSnapshot& snapshot)
{
    auto* deterministic = dynamic_cast<base::DeterministicIdGenerator*>(&ids);
    if (deterministic == nullptr) {
        return 0;
    }
    io::DocumentFile file;
    file.snapshot = snapshot;
    const std::uint64_t used = MaxDeterministicCounterIn(io::WriteDocumentJson(file));
    deterministic->SkipPast(used);
    return deterministic->Counter();
}

} // namespace kachakacha::v2::app
