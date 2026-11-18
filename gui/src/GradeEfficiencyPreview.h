#pragma once
#include <QWidget>
#include <QString>
#include <QColor>
#include <functional>
#include <vector>
#include "ScreenUnit.h"

// Selected grade efficiency model as a typeset formula (rich text for a
// QLabel). Below it the reduced size x: its formula as rich text, e.g.
// "<i>x</i> = <i>d</i> / <i>x</i><sub>cut</sub>", in large dark type,
// followed by its meaning in plain text in smaller type.
QString GradeEfficiencyFormulaHtml(stosis::EScreenModel model, const QString& xFormula, const QString& xMeaning);

// Same frame for every formula label (as the card density formula).
inline constexpr const char* kFormulaLabelStyle =
    "QLabel { background: #f7f6fd; border: 1px solid #d9d5f2; border-radius: 4px; padding: 6px; }";

// Grade efficiency T(d) of a separator over the size range of the grid
// (screen, classifier): one or several curves, size axis logarithmic
// (linear if the range starts at 0). Each curve can mark a size dashed
// (x_cut of the screen, d with T = 0.5 per compound of the classifier).
// With several curves a legend shows their labels.
class GradeEfficiencyPreview : public QWidget
{
public:
    struct SCurve
    {
        QString label;                      // legend; empty with a single curve
        std::function<double(double)> share; // T for a size in m, 0 ... 1
        double markerSize = 0.0;            // m; 0 = no marker
        QString markerLabel;
    };

    explicit GradeEfficiencyPreview(const QString& axisTitle, QWidget* parent = nullptr);

    // Curves over sizeMin ... sizeMax [m]; without curves the message is shown instead.
    void SetCurves(std::vector<SCurve> curves, double sizeMin, double sizeMax, const QString& message = QString());

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor CurveColor(size_t index) const;

    QString m_axisTitle;
    std::vector<SCurve> m_curves;
    double m_sizeMin = 0.0;
    double m_sizeMax = 0.0;
    QString m_message;
};
