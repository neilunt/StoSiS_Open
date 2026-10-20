#pragma once
#include <QWidget>
#include <QString>
#include <vector>

// Eine Kurve: Name (fuer die Legende) und ein Wert pro Zeitschritt. Die
// Zeitpunkte selbst sind implizit ueber SetTimeAxis gegeben (gemeinsames
// Gitter fuer alle Kurven).
struct SCurveSeries
{
    QString name;
    std::vector<double> values;             // Hauptlinie (Mittelwert bzw. einziger Lauf)
    std::vector<double> lower;              // optional: Streuband unten  (leer = kein Band)
    std::vector<double> upper;              // optional: Streuband oben
    std::vector<std::vector<double>> runs;  // optional: Einzellaeufe als duenne Linien
};

// Zeichnet mehrere Zeitreihen gemeinsam in einem Koordinatensystem, jede
// in eigener Farbe mit Legende - auf Wunsch mit halbtransparentem
// Streuband und duennen Einzellauf-Linien darunter. Rechnet selbst
// nichts, bekommt fertige Werte uebergeben.
class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveWidget(QWidget* parent = nullptr);

    void SetTimeAxis(double totalTime, size_t numTimeSteps);
    void SetSeries(std::vector<SCurveSeries> series);
    // Erklaerung unter der Legende, z.B. "Band: +-1 sigma, N = 20".
    void SetCaption(const QString& caption);
    // Beschriftung der Werteachse (z.B. "Temperatur [K]"); zeroBased = Achse
    // beginnt bei 0 (Mengen), sonst nur der Bereich der Werte (T, p).
    void SetValueAxis(const QString& label, bool zeroBased);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMarginLeft = 60;
    static constexpr int kMarginBottom = 40;
    static constexpr int kMarginTop = 15;
    // Platz fuer die Legende. Gleich dem rechten Rand der Heatmap mit
    // Randplots (70 + 90 + 8), damit die Zeitachsen bei gleicher
    // Fensterbreite exakt untereinander liegen.
    static constexpr int kMarginRight = 168;

    QColor ColorForIndex(int index) const;

    double m_totalTime = 1.0;
    size_t m_numTimeSteps = 0;
    std::vector<SCurveSeries> m_series;
    QString m_caption;
    QString m_valueLabel;
    bool m_zeroBased = true;
};
