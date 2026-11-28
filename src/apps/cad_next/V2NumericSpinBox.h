#pragma once
#include <QDoubleSpinBox>
#include <QLocale>
#include <QWidget>

// A partial number ("0.", "-0.") is a draft, never a geometry change.
// Set this before connecting callbacks, including dialogs opened during a job.
class V2NumericSpinBox final : public QDoubleSpinBox {
public:
    enum class Precision { Default, ExtrudeLength };

    explicit V2NumericSpinBox(QWidget* parent = nullptr,
        Precision precision = Precision::Default)
        : QDoubleSpinBox(parent), precision_(precision)
    {
        setKeyboardTracking(false);
        if (precision_ == Precision::ExtrudeLength) {
            setDecimals(9);
        }
    }

protected:
    QString textFromValue(double value) const override
    {
        QString text = QDoubleSpinBox::textFromValue(value);
        if (precision_ == Precision::ExtrudeLength) {
            const QString decimal = locale().decimalPoint();
            const qsizetype point = text.lastIndexOf(decimal);
            if (point >= 0) {
                const qsizetype minimumLength = point + decimal.size() + 3;
                const QChar zero = locale().zeroDigit().front();
                while (text.size() > minimumLength && text.endsWith(zero)) {
                    text.chop(1);
                }
            }
        }
        return text;
    }

private:
    Precision precision_;
};
