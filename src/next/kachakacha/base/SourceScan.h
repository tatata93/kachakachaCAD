#pragma once

//! V2のコード衛生検査(AT-ARC-001 / AT-ARC-005)で使う、ソース木の走査。
//! 幾何にもQtにも依存しないので base に置く。

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace kachakacha::v2::base {

struct SourceFile {
    std::filesystem::path path;
    std::vector<std::string> lines;
};

//! 拡張子が .h/.hpp/.cpp のファイルだけを集める。順序は決定的(パスの辞書順)。
[[nodiscard]] inline std::vector<SourceFile> CollectSourceFiles(
    const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::exists(root)) {
        return {};
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".hpp" && extension != ".cpp") {
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<SourceFile> files;
    files.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        SourceFile file;
        file.path = path;
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            file.lines.push_back(line);
        }
        files.push_back(std::move(file));
    }
    return files;
}

//! 行がコメントの中にあるかどうかは見ない。#include 行だけを素朴に取り出す。
[[nodiscard]] inline bool IsIncludeOf(const std::string& line, std::string_view needle)
{
    const std::size_t hash = line.find('#');
    if (hash == std::string::npos) {
        return false;
    }
    if (line.compare(hash, 8, "#include") != 0) {
        return false;
    }
    return line.find(needle) != std::string::npos;
}

//! 波括弧の深さを追って、深さ0から1へ入った位置から0へ戻るまでの行数を測る。
//! 名前空間とクラスは開き括弧の行に namespace/class/struct/enum があるので数えない。
[[nodiscard]] inline int LongestFunctionLength(const std::vector<std::string>& lines)
{
    int depth = 0;
    int blockStart = -1;
    int longest = 0;
    bool blockIsFunction = false;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        for (const char character : line) {
            if (character == '{') {
                if (depth == 0) {
                    blockStart = static_cast<int>(index);
                    const bool isScope = line.find("namespace") != std::string::npos
                        || line.find("class ") != std::string::npos
                        || line.find("struct ") != std::string::npos
                        || line.find("enum ") != std::string::npos
                        || line.find("union ") != std::string::npos
                        || line.find('=') != std::string::npos;
                    blockIsFunction = !isScope;
                }
                ++depth;
            } else if (character == '}') {
                if (depth > 0) {
                    --depth;
                }
                if (depth == 0 && blockStart >= 0 && blockIsFunction) {
                    const int length = static_cast<int>(index) - blockStart + 1;
                    if (length > longest) {
                        longest = length;
                    }
                    blockStart = -1;
                    blockIsFunction = false;
                }
            }
        }
    }
    return longest;
}

} // namespace kachakacha::v2::base
