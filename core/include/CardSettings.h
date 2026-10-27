#pragma once
#include <cmath>
#include <cstddef>

namespace stosis
{
    // Einstellungen der Karten im Partikelmodus (Methoden-Dokument 13.19,
    // 13.21).
    struct SCardSettings
    {
        size_t cardsPerSlice = 2000;     // N: Karten je Stromscheibe (je Inlet und Zeitschritt)
        double reservedFraction = 0.2;   // f: Anteil von N fuer die Mindestzahl je belegtem Bereich
        double rangeFactor = 2.0;        // Groessenbereiche: Grenzen rangeAnchor * rangeFactor^k ...
        double rangeAnchor = 1e-6;       // ... durch diese Groesse in m (1 um), unabhaengig vom Raster
        double occupiedThreshold = 1e-3; // belegt ab 0,1 % der Masse oder der Anzahl
        // Speichergrenze der Kartenansicht: Karten von Lauf 1, alle Stroeme zusammen.
        size_t historyBudgetBytes = size_t(256) << 20;
        // Noch nicht umgesetzt: Hoechstzahl = 2 x Sollzahl, Vorrat = 10 x N (Kap. 15).
    };

    // Groessenbereich k = [anchor * factor^k, anchor * factor^(k+1)) einer Groesse d > 0.
    inline int GetCardRangeIndex(double size, const SCardSettings& settings)
    {
        return static_cast<int>(std::floor(std::log(size / settings.rangeAnchor) / std::log(settings.rangeFactor)));
    }

    inline double GetCardRangeLower(int index, const SCardSettings& settings)
    {
        return settings.rangeAnchor * std::pow(settings.rangeFactor, index);
    }
}
