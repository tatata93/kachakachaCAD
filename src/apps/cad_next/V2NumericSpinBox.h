#pragma once
#include <QDoubleSpinBox>
#include <QLocale>
#include <QSpinBox>
#include <QWidget>
#include <algorithm>

inline QString NormalizeV2NumericText(QString text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto character = text.at(i).unicode();
        if (character >= 0xff01 && character <= 0xff5e)
            text[i] = QChar(character - 0xfee0);
        else if (character == 0x3000)
            text[i] = QLatin1Char(' ');
        else if (character == 0x2212)
            text[i] = QLatin1Char('-');
    }
    return text.trimmed();
}

inline QString CanonicalV2NumericText(const QString& draft, const QString& prefix, const QString& suffix)
{
    auto text = NormalizeV2NumericText(draft);
    const auto expectedPrefix = NormalizeV2NumericText(prefix);
    if (!expectedPrefix.isEmpty() && text.startsWith(expectedPrefix))
        text = text.sliced(expectedPrefix.size()).trimmed();
    const auto expectedSuffix = NormalizeV2NumericText(suffix);
    if (!expectedSuffix.isEmpty() && text.endsWith(expectedSuffix))
        text = text.chopped(expectedSuffix.size()).trimmed();
    // Qt can retain its suffix after pasting a number that already includes its unit.
    const auto unit = expectedSuffix.section(QLatin1Char(' '), 0, 0);
    if (!unit.isEmpty() && text.endsWith(unit))
        text = text.chopped(unit.size()).trimmed();
    return prefix + text + suffix;
}

// A partial number ("0.", "-0.") is a draft, never a geometry change.
// Set this before connecting callbacks, including dialogs opened during a job.
class V2NumericSpinBox final : public QDoubleSpinBox {
public:
    enum class Precision { Default, ExtrudeLength };

    explicit V2NumericSpinBox(QWidget* parent = nullptr,
        Precision precision = Precision::Default)
        : QDoubleSpinBox(parent)
    {
        setKeyboardTracking(false);
        if (precision == Precision::ExtrudeLength) {
            setDecimals(9);
        }
    }

protected:
    QString textFromValue(double value) const override
    {
        QString text = QDoubleSpinBox::textFromValue(value);
        const QString decimal = locale().decimalPoint();
        const qsizetype point = text.lastIndexOf(decimal);
        if (point >= 0) {
            const QString zero = locale().zeroDigit();
            const qsizetype decimalEnd = point + decimal.size();
            while (text.size() > decimalEnd && text.endsWith(zero)) text.chop(zero.size());
            if (text.size() == decimalEnd) text.truncate(point);
        }
        return text;
    }

    QValidator::State validate(QString& input, int& position) const override
    {
        auto normalized = CanonicalV2NumericText(input, prefix(), suffix());
        int localPosition = std::min(position, static_cast<int>(normalized.size()));
        return QDoubleSpinBox::validate(normalized, localPosition);
    }

    double valueFromText(const QString& text) const override
    {
        return QDoubleSpinBox::valueFromText(CanonicalV2NumericText(text, prefix(), suffix()));
    }
};

class V2IntegerSpinBox final : public QSpinBox {
public:
    explicit V2IntegerSpinBox(QWidget* parent = nullptr) : QSpinBox(parent)
    {
        setKeyboardTracking(false);
    }

protected:
    QValidator::State validate(QString& input, int& position) const override
    {
        auto normalized = CanonicalV2NumericText(input, prefix(), suffix());
        int localPosition = std::min(position, static_cast<int>(normalized.size()));
        return QSpinBox::validate(normalized, localPosition);
    }

    int valueFromText(const QString& text) const override
    {
        return QSpinBox::valueFromText(CanonicalV2NumericText(text, prefix(), suffix()));
    }
};
