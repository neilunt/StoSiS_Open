#pragma once
#include <QWidget>
#include <QString>
#include <QPoint>
#include <QRectF>
#include <vector>

// Eine Kurve: Name, Einheit und Stoff (fuer die Legende) und ein Wert pro Zeitschritt. Die
// Zeitpunkte selbst sind implizit ueber SetTimeAxis gegeben (gemeinsames
// Gitter fuer alle Kurven).
struct SCurveSeries
{
    QString name;                           // full name (CSV export)
    // Legend: the colour stands for the unit, the line style for the compound.
    QString unit;
    QString compound;                       // empty = no compound split (temperature, pressure)
    int colorIndex = 0;
    int styleIndex = 0;
    std::vector<double> values;             // Hauptlinie (Mittelwert bzw. einziger Lauf)
    std::vector<double> lower;              // optional: Streuband unten  (leer = kein Band)
    std::vector<double> upper;              // optional: Streuband oben
    std::vector<std::vector<double>> runs;  // optional: Einzellaeufe als duenne Linien
};

// Draws several time series in one coordinate system, each in its own colour
// with a legend below (colour = unit, line style = compound), optionally with
// a translucent band and thin single-run lines. Computes nothing itself, it
// gets finished values.
//
// Mouse: left drag pans, right drag spans a rectangle that is zoomed to, the
// wheel zooms around the cursor, a double click shows everything again. The
// view stays when the curves are refreshed with the same names (e.g. another
// band or a new simulation) and is reset when other curves are shown.
class CurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CurveWidget(QWidget* parent = nullptr);

    void SetTimeAxis(double totalTime, size_t numTimeSteps);
    void SetSeries(std::vector<SCurveSeries> series);
    // Note at the end of the legend, e.g. "1 Lauf".
    void SetCaption(const QString& caption);
    // Beschriftung der Werteachse (z.B. "Temperatur [K]"); zeroBased = Achse
    // beginnt bei 0 (Mengen), sonst nur der Bereich der Werte (T, p).
    void SetValueAxis(const QString& label, bool zeroBased);

    // Back to the full view (all times, all values).
    void ResetView();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    static constexpr int kMarginLeft = 70;
    static constexpr int kMarginBottom = 40; // axis labels; the legend comes below
    static constexpr int kMarginTop = 15;
    static constexpr int kMarginRight = 20;

    QColor ColorForIndex(int index) const;
    static Qt::PenStyle StyleForIndex(int index);
    // Legend below the plot: units with their colour, then compounds with
    // their line style, each wrapped to the width, the caption at the end.
    // Returns the height; painter = nullptr only measures.
    int Legend(QPainter* painter, int top) const;
    QRect PlotRect() const;
    // Full view: all times; values from all curves, bands and runs (mass
    // flows from 0, temperature and pressure only their range).
    QRectF AutoView() const;
    // View in data coordinates: x = time [s], y = value (top = y + height).
    QRectF CurrentView() const { return m_viewSet ? m_view : AutoView(); }
    QPointF ToData(const QPointF& pixel, const QRectF& view) const;
    void SetView(const QRectF& view);

    double m_totalTime = 1.0;
    size_t m_numTimeSteps = 0;
    std::vector<SCurveSeries> m_series;
    QString m_caption;
    QString m_valueLabel;
    bool m_zeroBased = true;

    // Zoomed or panned view; m_viewSet = false shows everything.
    bool m_viewSet = false;
    QRectF m_view;
    // Mouse interaction: pan with the left, rectangle with the right button.
    enum class EDrag { None, Pan, Zoom };
    EDrag m_drag = EDrag::None;
    QPoint m_pressPos;
    QPoint m_currentPos;
    QRectF m_pressView;
};
