#pragma once

#include "kachakacha/app/ShapeRebuild.h"
#include <memory>
#include <string>
#include <vector>

class V2MainWindow;
struct V2PreparedDocument;

// Optional, implementation-local acceleration. Definitions remain authoritative;
// the cache contains only bounded, validated native snapshots, never live handles.
class V2EvaluationCache final {
public:
    V2EvaluationCache(V2MainWindow&, const std::vector<kachakacha::v2::app::ShapeRebuildStep>&);
    ~V2EvaluationCache();
    V2EvaluationCache(const V2EvaluationCache&)=delete;
    V2EvaluationCache& operator=(const V2EvaluationCache&)=delete;
    [[nodiscard]] bool Available(const kachakacha::v2::app::ShapeRebuildStep&) const;
    [[nodiscard]] bool Restore(const kachakacha::v2::app::ShapeRebuildStep&);
    static void IndexPrepared(V2MainWindow&,const std::string& source,const V2PreparedDocument&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
