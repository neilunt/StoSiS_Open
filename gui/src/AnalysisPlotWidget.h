#pragma once
#include <QWidget>
#include <QString>
#include <utility>
#include <vector>
#include "SignalAnalysis.h"

class QPainter;

// Ein Diagramm des Auswertungsfensters (AnalysisWindow): Dichte ueber dem
// Wert (Histogramm), Wahrscheinlichkeitsnetz oder Zeitverlauf. Rechnet
// selbst nichts, bekommt fertige Werte uebergeben (SignalAnalysis.h).
class AnalysisPlotWidget : public QWidget
{
    Q_OBJECT

public:
    enum class EKind
    {
        Density,
        Probability,
        TimeSeries
    };

    explicit AnalysisPlotWidget(EKind kind, QWidget* parent = nullptr);

    // Einheit der Werte fuer die Achsenbeschriftung, z.B. "kg/s".
    void SetUnit(const QString& unit);
    // Statt eines Diagramms nur einen Hinweis zeigen.
    void ShowMessage(const QString& message);

    // Histogramm des ersten Laufs, Band = kleinste/groesste Dichte je Klasse
    // ueber alle Laeufe (leer = kein Band), Normalkurve aus mean/stdDev.
    void SetDensity(const stosis::SHistogram& firstRun, std::vector<double> bandLow,
        std::vector<double> bandHigh, double mean, double stdDev);
    // Punkte (Wert, Normalquantil) und Gerade der Normalverteilung mean/stdDev.
    void SetProbability(std::vector<std::pair<double, double>> points, double mean, double stdDev);
    // Werte je Zeitschritt (duenn) und Fenstermittel als Treppe (dick):
    // Fenster i beginnt bei windowStarts[i] und dauert windowLength.
    void SetTimeSeries(std::vector<double> times, std::vector<double> values,
        std::vector<double> windowStarts, std::vector<double> windowMeans, double windowLength);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void PaintDensity(QPainter& painter, const QRect& plot);
    void PaintProbability(QPainter& painter, const QRect& plot);
    void PaintTimeSeries(QPainter& painter, const QRect& plot);

    EKind m_kind;
    QString m_unit;
    QString m_message = "Noch keine Ergebnisse";

    stosis::SHistogram m_histogram;
    std::vector<double> m_bandLow, m_bandHigh;
    std::vector<std::pair<double, double>> m_points;
    std::vector<double> m_times, m_values, m_windowStarts, m_windowMeans;
    double m_windowLength = 0.0;
    double m_mean = 0.0;
    double m_stdDev = 0.0;
};
