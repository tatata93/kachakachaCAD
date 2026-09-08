//! 見本の `.kcd2` を書き出す道具(WP-12)。
//!
//! 試験では作らない。試験が置き場所を書き換えると、
//! 「置き直し忘れ」を落とす試験そのものが意味を失うためである。
#include "kachakacha/app/SampleDocument.h"
#include "kachakacha/io/AtomicFile.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "使い方: kachakacha_v2_write_sample <出す先の .kcd2>\n";
        return 2;
    }
    const auto archive = kachakacha::v2::app::BuildSampleArchive();
    if (!archive.HasValue()) {
        for (const auto& diagnostic : archive.Diagnostics()) {
            std::cerr << diagnostic.code << ' ' << diagnostic.summaryJa << '\n';
        }
        return 1;
    }
    const auto written = kachakacha::v2::io::WriteFileAtomically(argv[1], archive.Value());
    if (!written.HasValue()) {
        for (const auto& diagnostic : written.Diagnostics()) {
            std::cerr << diagnostic.code << ' ' << diagnostic.summaryJa << '\n';
        }
        return 1;
    }
    std::cout << argv[1] << " へ " << archive.Value().size() << " バイト書きました\n";
    return 0;
}
