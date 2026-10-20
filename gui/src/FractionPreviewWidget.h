#pragma once
#include <QWidget>
#include <QString>
#include <vector>

// Kleine Vorschau fuer den Splitter-Editor: zeigt den zeitlichen Verlauf
// der Anteile aller Ausgaenge (durchgezogen) und ihre Mittelwerte
// (gestrichelt) ueber die Simulationsdauer.
class FractionPreviewWidget : public QWidget
{
public:
    explicit FractionPreviewWidget(QWidget* parent = nullptr);

    // series[i][k] = Anteil von Ausgang i im Schritt k.
    void SetSeries(std::vector<std::vector<double>> series, std::vector<double> means,
        std::vector<QString> names, double totalTime);
    void Clear();

    QSize minimumSizeHint() const override { return QSize(260, 150); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<std::vector<double>> m_series;
    std::vector<double> m_means;
    std::vector<QString> m_names;
    double m_totalTime = 0.0;
};
