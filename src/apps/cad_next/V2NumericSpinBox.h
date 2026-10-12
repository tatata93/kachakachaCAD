#pragma once
#include <QDoubleSpinBox>
#include <QWidget>

// A partial number ("0.", "-0.") is a draft, never a geometry change.
// Set this before connecting callbacks, including dialogs opened during a job.
class V2NumericSpinBox final : public QDoubleSpinBox {
public:
    explicit V2NumericSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setKeyboardTracking(false);
    }
};
