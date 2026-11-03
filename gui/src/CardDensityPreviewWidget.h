#pragma once
#include <QString>
#include <QWidget>
#include "CardPlacement.h"

class QPainter;

// Vorschau der Kartendichte fuer eine Beispielverteilung (Einstellungen,
// Reiter "Karten", Arbeitsdokument 13.26):
//   oben  - Masse q3*, Anzahl q0* und Kartendichte c ueber ln d (je Flaeche 1),
//           unter d_min zusaetzlich die Anzahl mit echter Groesse (gestrichelt),
//           darunter jede Karte als Strich (Kartenteppich),
//   unten - Masse und Partikelzahl je Karte relativ zum Mittel (logarithmisch).
// Die Kennzahlen (Anteil der Karten an den Raendern, schwerste und
// partikelreichste Karte) liefert SummaryHtml fuer ein Textfeld darunter.
// Platziert wird genau wie am Inlet (PlaceCardsAtQuantiles).
class CardDensityPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CardDensityPreviewWidget(QWidget* parent = nullptr);

    // countMin: kleinste positive Stuetzstelle des Rasters (Partikelzahl wie
    // in CardNumberWeight).
    void SetData(const stosis::SSizeDistribution& distribution, const stosis::SCardSettings& settings, double countMin);

    // Kennzahlen als Rich Text, nur in Prozent und Vielfachen des Mittels.
    QString SummaryHtml() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    stosis::SCardPlacement m_placement;
    double m_countMin = 0.0;
};
