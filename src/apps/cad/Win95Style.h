#pragma once

#include <QFont>
#include <QPalette>
#include <QProxyStyle>

//! Windows 95 風の見た目(オーナー指示。通常の見た目と切り替えられる)。
//!
//! 一次資料: "The Windows Interface Guidelines for Software Design"
//! (Microsoft Press, 1995) 第13章 Visual Design の
//! Basic Border Styles / Button Border Styles / Field Border Style と、
//! Win32 `DrawEdge` の BDR_/EDGE_ 定義、Control Panel\Colors の既定値に従う。
//!
//! 縁は必ず「外側の辺」+「内側の辺」の2重で描く:
//!  - 浮き上がり: 外 左上=白 / 右下=黒、 内 左上=#DFDFDF / 右下=#808080
//!  - 押し込み  : 外 左上=#808080 / 右下=白、 内 左上=黒 / 右下=#DFDFDF
//!  - 彫り込み  : 左上=#808080 / 右下=白(1pxずつ)
class Win95Style final : public QProxyStyle {
    Q_OBJECT

public:
    Win95Style();

    //! Windows 標準スキームの配色。
    [[nodiscard]] static QPalette Win95Palette();
    //! MS UI Gothic 9pt(日本語版 Windows 95 の既定)。無ければ順に代替。
    [[nodiscard]] static QFont Win95Font();

    void drawPrimitive(
        PrimitiveElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget = nullptr) const override;
    void drawControl(
        ControlElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget = nullptr) const override;
    void drawComplexControl(
        ComplexControl control,
        const QStyleOptionComplex* option,
        QPainter* painter,
        const QWidget* widget = nullptr) const override;
    [[nodiscard]] int pixelMetric(
        PixelMetric metric,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr) const override;
    [[nodiscard]] int styleHint(
        StyleHint hint,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr,
        QStyleHintReturn* returnData = nullptr) const override;
    [[nodiscard]] QSize sizeFromContents(
        ContentsType type,
        const QStyleOption* option,
        const QSize& contentsSize,
        const QWidget* widget = nullptr) const override;
    void polish(QPalette& palette) override;
    void polish(QWidget* widget) override;
};
