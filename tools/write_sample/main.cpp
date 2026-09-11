//! 見本の `.kcd2` を書き出す道具(WP-12)。
//!
//! 試験では作らない。試験が置き場所を書き換えると、
//! 「置き直し忘れ」を落とす試験そのものが意味を失うためである。
#include "kachakacha/app/SampleDocument.h"
#include "kachakacha/app/RailwayNoseSample.h"
#include "kachakacha/io/AtomicFile.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const bool railwayNose = argc == 3 && std::string(argv[1]) == "--railway-nose";
    if ((!railwayNose && argc != 2) || (railwayNose && argc != 3)) {
        std::cerr << "使い方: kachakacha_v2_write_sample [--railway-nose] <出す先の .kcd2>\n";
        return 2;
    }
    const auto archive = railwayNose
        ? kachakacha::v2::app::BuildRailwayNoseSampleArchive()
        : kachakacha::v2::app::BuildSampleArchive();
    if (!archive.HasValue()) {
        for (const auto& diagnostic : archive.Diagnostics()) {
            std::cerr << diagnostic.code << ' ' << diagnostic.summaryJa << '\n';
        }
        return 1;
    }
    const char* output = railwayNose ? argv[2] : argv[1];
    const auto written = kachakacha::v2::io::WriteFileAtomically(output, archive.Value());
    if (!written.HasValue()) {
        for (const auto& diagnostic : written.Diagnostics()) {
            std::cerr << diagnostic.code << ' ' << diagnostic.summaryJa << '\n';
        }
        return 1;
    }
    std::cout << output << " へ " << archive.Value().size() << " バイト書きました\n";
    return 0;
}
