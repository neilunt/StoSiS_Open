#pragma once
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>
#include "CardDistribution.h"

class QPainter;

// Ein Diagramm der Kartenansicht (CardWindow):
//   Distribution - Verteilung aus den Karten: Dichte q3*(ln d) je feiner
//                  Klasse oder Summenkurve Q3, mit Vergleichskurve,
//   Rug          - Kartenteppich: jede Karte ein Punkt auf der d-Achse,
//   Classes      - Karten je feiner Klasse mit Warnschwelle,
//   TimeMap      - Verteilung ueber der Zeit als Farbbild,
//   Counts       - Kartenzahl je Zeitschritt,
//   Cloud        - Groesse gegen Masse jeder Karte (im Paket).
// Distribution, Rug, Classes und Cloud teilen die d-Achse (SetSizeAxis),
// TimeMap und Counts die Zeitachse; gleiche Raender links und rechts halten
// untereinander stehende Diagramme deckungsgleich. Rechnet selbst nur
// Achsen; Tooltips zeigen die Werte unter der Maus.
class CardPlotWidget : public QWidget
{
    Q_OBJECT

public:
    enum class EKind
    {
        Distribution,
        Rug,
        Classes,
        TimeMap,
        Counts,
        Cloud
    };

    struct SCard
    {
        double size = 0.0;  // m
        double mass = 0.0;  // Masse der Karte im Paket, kg
        int compound = 0;   // Stoff mit der groessten Masse
    };

    explicit CardPlotWidget(EKind kind, QWidget* parent = nullptr);

    // Statt eines Diagramms nur einen Hinweis zeigen.
    void ShowMessage(const QString& message);
    // Gemeinsame d-Achse (m, logarithmisch) und Grenzen der Groessenbereiche
    // des Siebsatzes (Streifen und Beschriftung).
    void SetSizeAxis(double sizeMin, double sizeMax, std::vector<double> rangeBounds);
    // Distribution: Klassen und Summenkurve aus den Karten; Vergleichskurve als Dichte
    // q3*(ln d) an Stuetzstellen (leer = keine).
    void SetDistribution(std::vector<stosis::SCardClass> classes, stosis::SCardCumulative cumulative,
                         std::vector<double> refSizes, std::vector<double> refDensity, QString refLabel);
    // Summenkurve statt Dichte zeigen.
    void SetCumulativeMode(bool cumulative);
    // Rug und Cloud: die Karten selbst.
    void SetCards(std::vector<SCard> cards, QStringList compounds);
    // Classes: Karten je Klasse; Klassen unter warnBelow Karten werden markiert.
    void SetClasses(std::vector<stosis::SCardClass> classes, size_t warnBelow);
    // Classes: Soll je Klasse nach der Kartendichte (Kartenzahl x Summe der
    // Anteile der Karten in der Klasse); leer = keine Linien.
    void SetClassTargets(std::vector<double> targets);
    // TimeMap: values[Spalte * Klassen + Klasse] = q3*(ln d); Spalte j gehoert zum
    // Zeitpunkt columnTimes[j] und ist columnWidth s breit; bounds =
    // Klassengrenzen (m); die Zeitachse reicht von 0 bis endTime.
    void SetTimeMap(std::vector<double> columnTimes, double columnWidth, double endTime,
                    std::vector<double> bounds, std::vector<double> values);
    // Kartenzahl je Zeitschritt.
    void SetCounts(std::vector<double> times, std::vector<double> counts);
    // Gewaehlter Zeitpunkt (Counts und TimeMap).
    void SetMarkTime(double time);

    // Farbe eines Stoffs (wie die Kurven im Kurvenfenster).
    static QColor CompoundColor(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    int TopMargin() const;
    int BottomMargin() const;
    double SizeToX(double size, const QRect& plot) const;
    double XToSize(double x, const QRect& plot) const;
    void PaintSizeAxis(QPainter& painter, const QRect& plot, bool labels) const;
    QString ClassTip(const stosis::SCardClass& c) const;

    void PaintDensity(QPainter& painter, const QRect& plot);
    void PaintCumulative(QPainter& painter, const QRect& plot);
    void PaintRug(QPainter& painter, const QRect& plot);
    void PaintClasses(QPainter& painter, const QRect& plot);
    void PaintTimeMap(QPainter& painter, const QRect& plot);
    void PaintCounts(QPainter& painter, const QRect& plot);
    void PaintCloud(QPainter& painter, const QRect& plot);

    // Stelle fuer einen Tooltip: Rechteck in Pixeln und Text.
    struct SHit
    {
        QRectF rect;
        QString text;
    };

    EKind m_kind;
    QString m_message = "Noch keine Karten";
    double m_sizeMin = 1e-6, m_sizeMax = 1e-3;
    std::vector<double> m_rangeBounds;
    std::vector<SCard> m_cards;
    QStringList m_compounds;
    std::vector<stosis::SCardClass> m_classes;
    stosis::SCardCumulative m_cumulative;
    std::vector<double> m_refSizes, m_refDensity, m_refCumulative;
    QString m_refLabel;
    bool m_cumulativeMode = false;
    size_t m_warnBelow = 10;
    std::vector<double> m_classTargets; // je Klasse, parallel zu m_classes
    std::vector<double> m_mapTimes, m_mapBounds, m_mapValues;
    double m_mapColumnWidth = 0.0, m_mapMax = 0.0, m_timeEnd = 0.0;
    std::vector<double> m_times, m_counts;
    double m_markTime = 0.0;
    std::vector<SHit> m_hits; // beim Zeichnen gefuellt
    QRect m_plot;             // Diagrammrahmen beim letzten Zeichnen
};
