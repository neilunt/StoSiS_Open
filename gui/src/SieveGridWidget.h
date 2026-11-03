#pragma once
#include <QWidget>
#include "CardSettings.h"
#include "SizeGrid.h"

// Reiter "Raster": Groessenraster und Siebsatz auf derselben Achse. Oben die
// Stuetzstellen des Rasters (Aufloesung von Kontinuum und Anzeige), darunter
// die Groessenbereiche 1 um * 2^k des Siebsatzes, die in der Kartenansicht die
// Klassen vorgeben. Beide sind unabhaengig:
// ein anderes Raster aendert die Karten nicht (Methodenhandbuch 4, 9.6).
class SieveGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SieveGridWidget(QWidget* parent = nullptr);

    void SetGrid(const stosis::SSizeGrid& sizes, const stosis::SCardSettings& cards);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    stosis::SSizeGrid m_sizes;
    stosis::SCardSettings m_cards;
};
