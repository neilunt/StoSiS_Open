#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

namespace stosis
{
    // Anzeige der Partikelgroesse: intern wird in m (SI) gerechnet, die
    // Oberflaeche zeigt um. Die Einheit steht als UTF-8-Bytes im Text: Ein
    // u8-Literal kodiert MSVC ohne /utf-8 (Kern und Tests) doppelt.
    inline constexpr double kSizeDisplayFactor = 1e6; // m -> um
    inline constexpr const char* kSizeDisplayUnit = "\xC2\xB5m"; // um mit Mikro-Zeichen

    enum class ESizeGridType
    {
        Linear,      // gleiche Abstaende Delta d
        Logarithmic  // geometrisch: gleiches Verhaeltnis r benachbarter Groessen
    };

    // Groessenraster: numClasses Stuetzstellen von sizeMin bis sizeMax (in m),
    // knotenbasiert - die erste liegt genau auf sizeMin, die letzte genau auf
    // sizeMax. Gespeichert wird an jeder Stuetzstelle die Dichte (z.B.
    // Massenstrom pro m).
    //   Linear:         d_i = sizeMin + i * Delta d
    //   Logarithmisch:  d_i = sizeMin * r^i,  r = (sizeMax / sizeMin)^(1 / (numClasses - 1))
    // Das logarithmische Raster hat gleich viele Stuetzstellen je Dekade und
    // braucht sizeMin > 0 (Methoden-Dokument Kapitel 2.5).
    struct SSizeGrid
    {
        ESizeGridType type = ESizeGridType::Logarithmic;
        size_t numClasses = 300;
        double sizeMin = 1e-6;
        double sizeMax = 1e-3;

        static SSizeGrid Linear(size_t numClasses, double sizeMin, double sizeMax)
        {
            return { ESizeGridType::Linear, numClasses, sizeMin, sizeMax };
        }

        static SSizeGrid Logarithmic(size_t numClasses, double sizeMin, double sizeMax)
        {
            return { ESizeGridType::Logarithmic, numClasses, sizeMin, sizeMax };
        }

        bool IsLogarithmic() const { return type == ESizeGridType::Logarithmic; }

        bool IsValid() const
        {
            return numClasses >= 2 && sizeMax > sizeMin && sizeMin >= 0.0 && (!IsLogarithmic() || sizeMin > 0.0);
        }

        // Schrittweite: Delta d (linear) bzw. Delta ln d = ln r (logarithmisch).
        double GetStep() const
        {
            if (numClasses < 2)
                return 0.0;
            const double n = static_cast<double>(numClasses - 1);
            if (IsLogarithmic())
                return (sizeMin > 0.0 && sizeMax > sizeMin) ? std::log(sizeMax / sizeMin) / n : 0.0;
            return (sizeMax - sizeMin) / n;
        }

        double GetSizeAt(size_t i) const
        {
            if (IsLogarithmic())
            {
                if (numClasses >= 2 && i + 1 == numClasses)
                    return sizeMax; // ohne Rundungsfehler von exp genau auf dem Rand
                return sizeMin * std::exp(static_cast<double>(i) * GetStep());
            }
            return sizeMin + static_cast<double>(i) * GetStep();
        }

        // Gewicht der Stuetzstelle i in der Trapezregel: Integral ueber d
        // = Summe w_i * q_i. Linear: Delta d. Logarithmisch: Trapez in ln d,
        // also Integral q dd = Integral (d q) d(ln d) -> w_i = d_i * ln r. In
        // d statt ln d gerechnet haette die Trapezregel auf dem geometrischen
        // Raster einen festen Fehler (+8.8e-5 bei 300 Stuetzstellen ueber
        // drei Dekaden), in ln d ist sie fuer glatte Verteilungen genauer als
        // 1e-5. Randpunkte zaehlen halb. Mit nur einer Stuetzstelle ist das
        // "Integral" der Wert selbst (Gewicht 1).
        double GetWeight(size_t i) const
        {
            if (numClasses < 2)
                return 1.0;
            const double half = (i == 0 || i + 1 == numClasses) ? 0.5 : 1.0;
            const double step = GetStep();
            return half * (IsLogarithmic() ? GetSizeAt(i) * step : step);
        }

        std::vector<double> GetWeights() const
        {
            std::vector<double> weights(numClasses);
            for (size_t i = 0; i < numClasses; ++i)
                weights[i] = GetWeight(i);
            return weights;
        }

        // Verhaeltnis benachbarter Groessen r (nur logarithmisch, sonst 0).
        double GetRatio() const { return IsLogarithmic() ? std::exp(GetStep()) : 0.0; }

        // Stuetzstellen je Dekade (nur logarithmisch, sonst 0).
        double GetClassesPerDecade() const
        {
            const double step = IsLogarithmic() ? GetStep() : 0.0;
            return step > 0.0 ? std::log(10.0) / step : 0.0;
        }

        // Lage einer Groesse auf der Achse: 0 bei sizeMin, 1 bei sizeMax
        // (logarithmisch: in ln d gleichmaessig). Fuer Achsenbeschriftungen.
        double GetAxisFraction(double size) const
        {
            if (IsLogarithmic())
                return (size > 0.0 && sizeMin > 0.0 && sizeMax > sizeMin) ? std::log(size / sizeMin) / std::log(sizeMax / sizeMin) : 0.0;
            return (sizeMax > sizeMin) ? (size - sizeMin) / (sizeMax - sizeMin) : 0.0;
        }

        bool operator==(const SSizeGrid& other) const
        {
            return type == other.type && numClasses == other.numClasses
                && sizeMin == other.sizeMin && sizeMax == other.sizeMax;
        }
    };
}
