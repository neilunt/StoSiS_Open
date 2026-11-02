#pragma once
#include <cmath>
#include <cstddef>

namespace stosis
{
    // Wie Masse und Partikelzahl zur Kartendichte gemittelt werden
    // (Methoden-Dokument 13.26); f = SCardSettings::numberShare.
    enum class ECardDensityRule
    {
        Sum,      // (1 - f) * Masse + f * Anzahl
        Product,  // Masse^(1 - f) * Anzahl^f
        Quadratic // ((1 - f) * Masse^2 + f * Anzahl^2)^(1/2)
    };

    inline const char* GetCardDensityRuleName(ECardDensityRule rule)
    {
        switch (rule)
        {
        case ECardDensityRule::Product:
            return "Produkt";
        case ECardDensityRule::Quadratic:
            return "Quadratisch";
        case ECardDensityRule::Sum:
        default:
            return "Summe";
        }
    }

    // Einstellungen der Karten im Partikelmodus (Methoden-Dokument 13.19,
    // 13.26).
    struct SCardSettings
    {
        size_t cardsPerSlice = 2000; // N: Karten je Stromscheibe (je Inlet und Zeitschritt)
        // Kartendichte c ~ Mittel(Masse, Anzahl)^exponent ueber ln d (CardDensity.h).
        ECardDensityRule rule = ECardDensityRule::Sum;
        double numberShare = 0.5;    // f: Gewicht der Partikelzahl, 0 = nur Masse, 1 = nur Anzahl
        double exponent = 0.8;       // gamma: Exponent der Abflachung, 1 = keine; kleiner = mehr Karten an den Raendern
        // Grenzen je Stromscheibe (CardLimits.h): hoechstens upperFactor x N
        // Karten (sonst zusammenlegen), keine Karte mit mehr als splitFactor / N
        // der Kartendichte (sonst teilen).
        bool sliceLimits = true;
        double upperFactor = 2.0;
        double splitFactor = 5.0;
        // Anzeigeklassen der Kartenansicht (Siebsatz): Grenzen rangeAnchor * rangeFactor^k.
        double rangeFactor = 2.0;
        double rangeAnchor = 1e-6;   // m (1 um), unabhaengig vom Raster
        // Speichergrenze der Kartenansicht: Karten von Lauf 1, alle Stroeme zusammen.
        size_t historyBudgetBytes = size_t(256) << 20;
        // Noch nicht umgesetzt: Vorrat = 10 x N (Kap. 15).
    };

    // Kleinstes gamma in der Oberflaeche: Darunter landen merklich Karten in
    // den aeussersten Auslaeufern, die kaum etwas tragen.
    inline constexpr double kCardExponentMinUi = 0.3;

    // Anzeigeklasse k = [anchor * factor^k, anchor * factor^(k+1)) einer Groesse d > 0.
    inline int GetCardRangeIndex(double size, const SCardSettings& settings)
    {
        const double x = size / settings.rangeAnchor;
        // Faktor 2 (Standard): Exponent der Gleitkommazahl, x = m * 2^e mit
        // m in [0,5; 1) - genau auch auf den Grenzen und ohne Logarithmus.
        if (settings.rangeFactor == 2.0 && x > 0.0 && std::isfinite(x))
        {
            int exponent = 0;
            std::frexp(x, &exponent);
            return exponent - 1;
        }
        return static_cast<int>(std::floor(std::log(x) / std::log(settings.rangeFactor)));
    }

    inline double GetCardRangeLower(int index, const SCardSettings& settings)
    {
        return settings.rangeAnchor * std::pow(settings.rangeFactor, index);
    }
}
