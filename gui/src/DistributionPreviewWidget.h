#pragma once
#include <QWidget>
#include "SizeDistribution.h"
#include "SizeGrid.h"

// Kleine Vorschau einer Groessenverteilung q(d) ueber genau dem
// Groessenraster des Flowsheets (x-Achse = d_min ... d_max). Gezeichnet
// werden die Werte an den Gitterpunkten - also exakt das, was die
// Simulation spaeter sieht. Zusaetzlich: Anteil der Masse, den das
// Raster erfasst (warnt, wenn die Verteilung abgeschnitten wird oder
// das Raster zu grob ist).
class DistributionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DistributionPreviewWidget(QWidget* parent = nullptr);

    void SetDistribution(const stosis::SSizeDistribution& distribution);
    void SetGrid(const stosis::SSizeGrid& sizes);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMarginLeft = 10;
    static constexpr int kMarginRight = 10;
    static constexpr int kMarginTop = 20;   // Platz fuer die Erfassungs-Anzeige
    static constexpr int kMarginBottom = 22; // Platz fuer die x-Achse

    stosis::SSizeDistribution m_distribution;
    stosis::SSizeGrid m_sizes; // Standardraster, bis SetGrid kommt
};
