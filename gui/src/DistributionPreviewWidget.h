#pragma once
#include <QWidget>
#include "CardSettings.h"
#include "SizeDistribution.h"
#include "SizeGrid.h"

class QPainter;

// Kleine Vorschau einer Groessenverteilung q(d) ueber genau dem
// Groessenraster des Flowsheets (x-Achse = d_min ... d_max). Gezeichnet
// werden die Werte an den Gitterpunkten - also exakt das, was die
// Simulation spaeter sieht. Zusaetzlich: Anteil der Masse, den das
// Raster erfasst (warnt, wenn die Verteilung abgeschnitten wird oder
// das Raster zu grob ist). Im Partikelmodus zusaetzlich der Siebsatz
// (Groessenbereiche 1 um * 2^k) und die Karten des Inlets als Staebe
// (Hoehe = Masse je Karte), mit der Kartenzahl je Bereich.
class DistributionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DistributionPreviewWidget(QWidget* parent = nullptr);

    void SetDistribution(const stosis::SSizeDistribution& distribution);
    void SetGrid(const stosis::SSizeGrid& sizes);
    // Partikelmodus: Karten wie am Inlet platzieren und einzeichnen.
    void SetCards(bool particles, const stosis::SCardSettings& settings);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Siebsatz und Karten ueber die Kurve zeichnen (nur Partikelmodus).
    void PaintCards(QPainter& painter, const QRect& plotRect);

    static constexpr int kMarginLeft = 10;
    static constexpr int kMarginRight = 10;
    static constexpr int kMarginTop = 20;   // Platz fuer die Erfassungs-Anzeige
    static constexpr int kMarginBottom = 22; // Platz fuer die x-Achse
    static constexpr size_t kMinCentralNodes = 5; // darunter Hinweis "Raster grob"

    stosis::SSizeDistribution m_distribution;
    stosis::SSizeGrid m_sizes; // Standardraster, bis SetGrid kommt
    bool m_particles = false;
    stosis::SCardSettings m_cardSettings;
};
