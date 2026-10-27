#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

class QPainter;

// Ein Diagramm der Kartenansicht (CardWindow): Kartenwolke (Groesse gegen
// Masse jeder Karte, Siebsatz als Streifen), Karten je Groessenbereich
// (Balken mit Warnschwelle) oder Kartenzahl je Zeitschritt. Rechnet
// selbst nichts; Tooltips zeigen die Werte unter der Maus.
class CardPlotWidget : public QWidget
{
    Q_OBJECT

public:
    enum class EKind
    {
        Cloud,
        Ranges,
        Counts
    };

    struct SCard
    {
        double size = 0.0;  // m
        double mass = 0.0;  // Massenstrom der Karte, kg/s
        int compound = 0;   // Stoff mit der groessten Masse
    };

    struct SRange
    {
        double lower = 0.0; // m
        double upper = 0.0; // m
        size_t cards = 0;
        double massShare = 0.0; // Anteil an der Masse des Stroms
    };

    explicit CardPlotWidget(EKind kind, QWidget* parent = nullptr);

    // Statt eines Diagramms nur einen Hinweis zeigen.
    void ShowMessage(const QString& message);
    // Karten eines Zeitschritts; bounds = Grenzen der Groessenbereiche (m, aufsteigend).
    void SetCloud(std::vector<SCard> cards, std::vector<double> bounds, QStringList compounds);
    // Karten je Bereich; Bereiche unter warnBelow Karten werden markiert.
    void SetRanges(std::vector<SRange> ranges, size_t warnBelow);
    // Kartenzahl je Zeitschritt, markTime = gewaehlter Zeitpunkt.
    void SetCounts(std::vector<double> times, std::vector<double> counts, double markTime);

    // Farbe eines Stoffs (wie die Kurven im Kurvenfenster).
    static QColor CompoundColor(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void PaintCloud(QPainter& painter, const QRect& plot);
    void PaintRanges(QPainter& painter, const QRect& plot);
    void PaintCounts(QPainter& painter, const QRect& plot);

    // Stelle fuer einen Tooltip: Rechteck in Pixeln und Text.
    struct SHit
    {
        QRectF rect;
        QString text;
    };

    EKind m_kind;
    QString m_message = "Noch keine Karten";
    std::vector<SCard> m_cards;
    std::vector<double> m_bounds;
    QStringList m_compounds;
    std::vector<SRange> m_ranges;
    size_t m_warnBelow = 10;
    std::vector<double> m_times, m_counts;
    double m_markTime = 0.0;
    std::vector<SHit> m_hits; // beim Zeichnen gefuellt
    QRect m_plot;             // Diagrammrahmen beim letzten Zeichnen
};
