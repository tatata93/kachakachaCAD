#pragma once
#include "V2PreparedDocument.h"
#include "kachakacha/io/DocumentFile.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
class V2MainWindow;

class V2DocumentOpen final:public QObject {
public:
    static bool Open(V2MainWindow& window,const QString& path);
    static bool Adopt(V2MainWindow& window,kachakacha::v2::document::DocumentSnapshot snapshot,
        const std::function<void()>& install={});
    static kachakacha::v2::io::DocumentFile FileForSave(V2MainWindow& window);
    static int Worker(const QString& directory);
    static void CacheSaved(V2MainWindow& window, const std::string& archive);
    static bool IsWorker();
    static void Progress(int completed,int total,const std::string& name);
private:
    explicit V2DocumentOpen(V2MainWindow& window);
    static V2DocumentOpen* State(V2MainWindow& window,bool create);
    static kachakacha::v2::base::Result<std::unique_ptr<V2PreparedDocument>> Capture(V2MainWindow& window,std::string archive);
    kachakacha::v2::io::DocumentFile extras_;
};
