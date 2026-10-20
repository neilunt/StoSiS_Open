#pragma once
#include <cmath>
#include <cstddef>
#include "SizeGrid.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace stosis
{
    // Verteilungstypen fuer die Groessenverteilung am Inlet. Alle sind
    // als Dichte q(d) definiert (Integral ueber alle d = 1), also als
    // MASSEN-Dichte, weil das Inlet einen Massenstrom liefert.
    enum class EDistributionType
    {
        Normal,     // Gauss: Lage d0, Breite sigma (beide in m)
        LogNormal,  // ln(d) normalverteilt: Median d50, Breite sigma_ln (dimensionslos)
        RRSB        // Rosin-Rammler-Sperling-Bennett: Lage d' (63,2 %), Gleichmaessigkeit n
    };

    // Beschriftung + sinnvolle Bereiche der zwei Parameter je Typ - damit
    // die GUI nichts ueber die einzelnen Verteilungen wissen muss. Alle
    // Groessen in m (SI); die Lage ist immer eine Groesse, die Breite nur
    // bei der Normalverteilung (sonst dimensionslos, widthIsSize = false).
    struct SDistributionParameterInfo
    {
        const char* typeName;
        const char* positionLabel;
        double positionMin;
        double positionMax;
        double positionDefault;
        const char* widthLabel;
        double widthMin;
        double widthMax;
        double widthDefault;
        bool widthIsSize;
    };

    inline SDistributionParameterInfo GetDistributionParameterInfo(EDistributionType type)
    {
        switch (type)
        {
        case EDistributionType::LogNormal:
            return { "Log-Normal", "Median d50", 1e-12, 1.0, 60e-6,
                     "Breite sigma_ln", 1e-3, 5.0, 0.1, false };
        case EDistributionType::RRSB:
            return { "RRSB (Rosin-Rammler)", "Lageparameter d' (63,2 %)", 1e-12, 1.0, 65e-6,
                     "Gleichmaessigkeit n", 0.1, 50.0, 8.0, false };
        case EDistributionType::Normal:
        default:
            return { "Normal (Gauss)", "Mittelwert d0", 0.0, 1.0, 100e-6,
                     "Standardabweichung sigma", 1e-12, 1.0, 25e-6, true };
        }
    }

    // Eine konkrete Groessenverteilung: Typ + zwei Parameter. Bedeutung
    // von position/width haengt vom Typ ab (siehe oben). Dichte q(d) in 1/m.
    struct SSizeDistribution
    {
        EDistributionType type = EDistributionType::Normal;
        double position = 100e-6; // m
        double width = 25e-6;     // m (Normal) bzw. dimensionslos

        // Dichte q(d). Fuer d <= 0 liefern LogNormal und RRSB 0 (keine
        // negativen Partikelgroessen); die Normalverteilung ist auch dort
        // definiert - bei grossem sigma relativ zu d0 geht dadurch Masse
        // "unter null" verloren, das zeigen CapturedFraction() und
        // FractionBelow() an.
        double Density(double d) const
        {
            switch (type)
            {
            case EDistributionType::LogNormal:
            {
                if (d <= 0.0 || position <= 0.0 || width <= 0.0)
                    return 0.0;
                const double z = std::log(d / position) / width;
                return std::exp(-0.5 * z * z) / (d * width * std::sqrt(2.0 * M_PI));
            }
            case EDistributionType::RRSB:
            {
                if (d <= 0.0 || position <= 0.0 || width <= 0.0)
                    return 0.0;
                const double x = d / position;
                const double xn = std::pow(x, width);
                return (width / position) * (xn / x) * std::exp(-xn);
            }
            case EDistributionType::Normal:
            default:
            {
                if (width <= 0.0)
                    return 0.0;
                const double z = (d - position) / width;
                return std::exp(-0.5 * z * z) / (width * std::sqrt(2.0 * M_PI));
            }
            }
        }

        // Verteilungssumme Q3(d): Massenanteil kleiner als d (exakt, ohne
        // Raster). Bei der Normalverteilung zaehlt auch der Teil unter 0 mit.
        double Cumulative(double d) const
        {
            switch (type)
            {
            case EDistributionType::LogNormal:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return d <= 0.0 ? 0.0 : 0.5 * std::erfc(-std::log(d / position) / (width * std::sqrt(2.0)));
            case EDistributionType::RRSB:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return d <= 0.0 ? 0.0 : 1.0 - std::exp(-std::pow(d / position, width));
            case EDistributionType::Normal:
            default:
                if (width <= 0.0)
                    return 0.0;
                return 0.5 * std::erfc(-(d - position) / (width * std::sqrt(2.0)));
            }
        }

        // Massenanteil unter sizeMin bzw. ueber sizeMax (exakt aus Q3) - das,
        // was ein Raster von sizeMin bis sizeMax nicht abdecken kann.
        double FractionBelow(double size) const { return Cumulative(size); }
        double FractionAbove(double size) const { return 1.0 - Cumulative(size); }

        // Anteil der Masse, den das Groessenraster tatsaechlich erfasst -
        // berechnet genau so, wie die Simulation spaeter integriert
        // (Dichte an den Gitterpunkten, Trapezregel mit den Gewichten des
        // Rasters). Weicht von 1 ab, wenn die Verteilung ueber den
        // Rasterrand hinausragt ODER das Raster zu grob fuer die
        // Verteilungsbreite ist.
        double CapturedFraction(const SSizeGrid& sizes) const
        {
            if (!sizes.IsValid())
                return 0.0;
            if (!sizes.IsLogarithmic())
            {
                // Linear: wie bisher erst summieren, dann mit Delta d malnehmen.
                double sum = 0.5 * (Density(sizes.GetSizeAt(0)) + Density(sizes.GetSizeAt(sizes.numClasses - 1)));
                for (size_t s = 1; s + 1 < sizes.numClasses; ++s)
                    sum += Density(sizes.GetSizeAt(s));
                return sum * sizes.GetStep();
            }
            double sum = 0.0;
            for (size_t s = 0; s < sizes.numClasses; ++s)
                sum += sizes.GetWeight(s) * Density(sizes.GetSizeAt(s));
            return sum;
        }
    };
}
