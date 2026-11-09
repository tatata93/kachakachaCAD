#pragma once
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Document.h"

// A wire consisting only of one smooth curved run is one editing target.
// Explicit endpoint/control-point/individual-edge tools bypass this promotion.
void V2PickWholeSmoothCurves(const kachakacha::v2::document::Document& document,
    std::vector<kachakacha::v2::app::PickCandidate>& candidates);
