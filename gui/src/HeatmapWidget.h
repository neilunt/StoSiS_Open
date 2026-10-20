#pragma once
#include <QWidget>
#include <vector>
#include "SimulationResults.h"

// Eigenes Widget, das eine CSimulationResults-Matrix als Heatmap
// zeichnet. Kein Fremd-Plotting-Framework - direkt ueber QImage/QPainter,
// damit klar bleibt, wie aus Zahlen ein Bild wird:
//   1. Werte -> Farbe ueber eine Colormap (hier: vereinfachtes "viridis")
//   2. Farb-Matrix wird als QImage aufgebaut
//   3. QImage wird skaliert auf die Widget-Groesse gezeichnet
// Achsen (Standard): x = Partikelgroesse, y = Zeit (oben = t=0, unten =
// t=Ende). Mit SetTimeOnXAxis(true) getauscht: x = Zeit, y = Groesse
// (unten = klein) - passend zum Kurvenvergleich, der auch Zeit auf x hat.
enum class EColorMode
{
    Sequential,     // dunkel nach hell (aktuell, fuer heatmaps)
    Diverging       // blau weiss rot (fuer Differenzbilder)
};

class HeatmapWidget : public QWidget
{
    Q_OBJECT



public:
    explicit HeatmapWidget(QWidget* parent = nullptr);

    void SetResults(const stosis::CSimulationResults* results);

    void SetColorMode(EColorMode mode) { m_colorMode = mode; update(); }

    void SetValueLabel(const QString& label) { m_valueLabel = label; update(); }

    // Einheit der Groessenachse: angezeigt wird Groesse * factor (m -> um mit
    // 1e6). Werte je Groesse (Matrix = Dichte je m, Randverteilung ueber die
    // Zeit = Masse je m) werden dafuer durch factor geteilt. Standard: 1 ohne
    // Einheit (alte Vergleichsansicht). Ein logarithmisches Raster bekommt
    // automatisch eine logarithmische Achse mit Dekaden.
    void SetSizeUnit(double factor, const QString& unit);

    // Logarithmische Farbskala (nur im Sequential-Modus): oberes Ende =
    // Maximum, unteres Ende = Maximum * 10^-decades. Alles darunter
    // (auch 0) bekommt die unterste Farbe.
    void SetLogScale(bool enabled);
    void SetLogDecades(int decades);

    void SetTimeOnXAxis(bool enabled);

    // Randverteilungen ("Joint Plot"): oben eine Kurve, die ueber die
    // vertikale Achse integriert (teilt sich die x-Achse mit der
    // Heatmap), rechts eine, die ueber die horizontale Achse integriert
    // (teilt sich die y-Achse). Je nach Achsenlage ist das der
    // Massenstrom ueber die Zeit bzw. die Groessenverteilung des
    // gesamten Durchsatzes.
    void SetShowMarginals(bool enabled);

    // Randverteilungen von aussen vorgeben statt aus der Matrix zu
    // integrieren - noetig z.B. fuer die Standardabweichung ueber mehrere
    // Laeufe: die Streuung des Massenstroms ist NICHT das Integral der
    // sigma-Matrix. overSize[t] = Wert ueber die Zeit, overTime[s] = Wert
    // ueber die Groesse; labelPrefix steht vor den Achsentiteln.
    void SetMarginalOverride(std::vector<double> overSize, std::vector<double> overTime,
        const QString& labelPrefix);
    void ClearMarginalOverride();

protected:
    void paintEvent(QPaintEvent* event) override;
    // Zeigt Zeit, Groesse und Wert der Zelle unter dem Mauszeiger als Tooltip.
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Reservierter Platz links/unten fuer Achsenbeschriftung, damit die
    // eigentliche Heatmap nicht bis an den Widget-Rand geht.
    static constexpr int kMarginLeft = 60;
    static constexpr int kMarginBottom = 40;
    static constexpr int kMarginTop = 15;
    static constexpr int kMarginRight = 70; // Platz fuer die Farbskala
    static constexpr int kMarginalSize = 90; // Hoehe bzw. Breite eines Randplots
    static constexpr int kMarginalGap = 8;   // Abstand Randplot <-> Heatmap

    EColorMode m_colorMode = EColorMode::Sequential;

    QColor ValueToColor(double normalizedValue) const;
    // Rohwert -> Bereich 0..1 (Sequential, linear oder log) bzw. -1..+1 (Diverging).
    double NormalizeValue(double value, double scaleMax) const;
    QRect PlotRect() const;
    QRect TopMarginalRect() const;
    QRect RightMarginalRect() const;

    // Berechnet beide Randverteilungen in Bild-Reihenfolge:
    // m_columnMarginal[c] fuer jede Bildspalte (links -> rechts),
    // m_rowMarginal[r] fuer jede Bildzeile (oben -> unten).
    void ComputeMarginals();
    void DrawMarginal(QPainter& painter, const QRect& rect,
                      const std::vector<double>& values, bool vertical) const;
    // Achsenwert (Zeit oder Groesse) fuer eine Bildspalte bzw. -zeile.
    double ColumnAxisValue(size_t column) const;
    double RowAxisValue(size_t row) const;
    QString ColumnMarginalLabel() const;
    QString RowMarginalLabel() const;
    QString SizeMarginalLabel() const;

    // Achsenmarke: Lage entlang der Achse (0 = Anfang, 1 = Ende) und
    // Beschriftung (leer = kleiner Strich ohne Zahl).
    struct SAxisTick
    {
        double fraction;
        QString label;
    };
    std::vector<SAxisTick> TimeAxisTicks() const;
    std::vector<SAxisTick> SizeAxisTicks() const;

    const stosis::CSimulationResults* m_results = nullptr;

    QString m_valueLabel = "Wert";
    double m_sizeFactor = 1.0;
    QString m_sizeUnit;

    bool m_logScale = false;
    int m_logDecades = 4;
    bool m_timeOnXAxis = false;
    bool m_showMarginals = false;

    std::vector<double> m_columnMarginal;
    std::vector<double> m_rowMarginal;

    bool m_useMarginalOverride = false;
    std::vector<double> m_overrideOverSize;
    std::vector<double> m_overrideOverTime;
    QString m_marginalLabelPrefix;
};
