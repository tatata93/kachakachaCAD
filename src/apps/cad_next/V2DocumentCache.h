#pragma once
#include "V2PreparedDocument.h"
#include <QString>
#include <optional>
#include <string>

// Disposable local data. Editable history remains exclusively in the KCD archive.
namespace V2DocumentCache {
QString Key(const std::string& source);
std::optional<V2PreparedDocument> Read(const std::string& source);
// Save-only existence check. Never authorizes adoption; Read still validates all fields.
bool HasMatchingPayload(const std::string& source);
bool Write(const std::string& source, V2PreparedDocument& data);
bool Transfer(const std::string& source, const QString& destination, const QString& token);
bool StoreTransfer(const std::string& source, const QString& transfer, const QString& token);
}
