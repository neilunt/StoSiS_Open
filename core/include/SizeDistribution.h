#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>
#include "SizeGrid.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace stosis
{
    // Quantil der Standardnormalverteilung: z mit Phi(z) = p. Startwert
    // nach Acklam (relativer Fehler etwa 1e-9), danach ein Halley-Schritt mit
    // erfc - damit auf etwa 1e-15 genau. p <= 0 bzw. >= 1 ergibt -/+ unendlich.
    inline double StandardNormalQuantile(double p)
    {
        if (!(p > 0.0))
            return -HUGE_VAL;
        if (!(p < 1.0))
            return HUGE_VAL;
        static const double a[] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
            1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
        static const double b[] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
            6.680131188771972e+01, -1.328068155288572e+01 };
        static const double c[] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
            -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
        static const double d[] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
            3.754408661907416e+00 };
        const double pLow = 0.02425;
        double x = 0.0;
        if (p < pLow || p > 1.0 - pLow)
        {
            const double q = std::sqrt(-2.0 * std::log(p < pLow ? p : 1.0 - p));
            x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
                / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
            if (p >= pLow)
                x = -x;
        }
        else
        {
            const double q = p - 0.5;
            const double r = q * q;
            x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
                / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
        }
        // Halley: e = Phi(x) - p, u = e / phi(x), x -= u / (1 + x u / 2).
        // Fuer p > 0,5 gleichwertig e = (1 - p) - (1 - Phi(x)): die obere
        // Flanke ohne Ausloeschung (1 - p ist dort exakt).
        const double e = (p > 0.5) ? (1.0 - p) - 0.5 * std::erfc(x / std::sqrt(2.0))
                                   : 0.5 * std::erfc(-x / std::sqrt(2.0)) - p;
        const double u = e * std::sqrt(2.0 * M_PI) * std::exp(0.5 * x * x);
        return x - u / (1.0 + 0.5 * x * u);
    }

    // Verteilungstypen fuer die Groessenverteilung am Inlet. Alle sind
    // als Dichte q(d) definiert (Integral ueber alle d = 1), also als
    // MASSEN-Dichte, weil das Inlet einen Massenstrom liefert. Standard ist
    // Log-Normal; die Normalverteilung ist waehlbar, aber mit Warnung
    // (GetDistributionWarning, Methoden-Dokument Kap. 3). Die Nummern
    // bleiben fest (neue Typen hinten anfuegen).
    enum class EDistributionType
    {
        Normal,     // Gauss: Lage d0, Breite sigma (beide in m) - fuer Groessen meist ungeeignet
        LogNormal,  // ln(d) normalverteilt: Median d50, Breite sigma_ln (dimensionslos)
        RRSB,       // Rosin-Rammler-Sperling-Bennett: Lage d' (63,2 %), Gleichmaessigkeit n
        GGS,        // Gates-Gaudin-Schuhmann: Q3 = (d / d_max)^m bis zum Groesstkorn d_max
        Table       // benutzerdefiniert: Punkte (d, Q3), dazwischen linear in ln d
    };

    // Ein Punkt der benutzerdefinierten Verteilung: Massenanteil Q3 unter d.
    struct SDistributionPoint
    {
        double size = 0.0;     // m
        double fraction = 0.0; // Q3 als Anteil 0 ... 1
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
        bool isTable = false; // Tabelle statt zweier Parameter
    };

    inline SDistributionParameterInfo GetDistributionParameterInfo(EDistributionType type)
    {
        switch (type)
        {
        case EDistributionType::LogNormal:
            return { "Log-Normal", "Median d50", 1e-12, 1.0, 100e-6,
                     "Breite sigma_ln", 1e-3, 5.0, 0.25, false };
        case EDistributionType::RRSB:
            return { "RRSB (Rosin-Rammler)", "Lageparameter d' (63,2 %)", 1e-12, 1.0, 65e-6,
                     "Gleichmaessigkeit n", 0.1, 50.0, 8.0, false };
        case EDistributionType::GGS:
            return { "GGS (Gates-Gaudin-Schuhmann)", "Groesstkorn d_max", 1e-12, 1.0, 200e-6,
                     "Verteilungsmodul m", 0.1, 50.0, 2.0, false };
        case EDistributionType::Table:
            return { "Tabelle (benutzerdefiniert)", "", 0.0, 0.0, 0.0, "", 0.0, 0.0, 0.0, false, true };
        case EDistributionType::Normal:
        default:
            return { "Normal (Gauss) - nicht empfohlen", "Mittelwert d0", 0.0, 1.0, 100e-6,
                     "Standardabweichung sigma", 1e-12, 1.0, 25e-6, true };
        }
    }

    // Hinweis zum gewaehlten Typ, leer = keiner. Die Normalverteilung ist
    // waehlbar, der Inlet-Editor zeigt dann diesen Text.
    inline const char* GetDistributionWarning(EDistributionType type)
    {
        if (type == EDistributionType::Normal)
            return "Die Normalverteilung erlaubt negative Groessen (d < 0) und liefert zu kleinen Groessen "
                   "beliebig viele Partikel - fuer Partikelgroessen meist ungeeignet. Besser Log-Normal "
                   "(fuer eine schmale Verteilung etwa sigma_ln = sigma / d0).";
        return "";
    }

    // Eine konkrete Groessenverteilung: Typ + zwei Parameter. Bedeutung
    // von position/width haengt vom Typ ab (siehe oben). Dichte q(d) in 1/m.
    // Standard: Log-Normal, d50 = 100 um, sigma_ln = 0,25 (aehnlich einer
    // Normalverteilung 100 um +- 25 um, aber ohne d < 0).
    struct SSizeDistribution
    {
        EDistributionType type = EDistributionType::LogNormal;
        double position = 100e-6; // m
        double width = 0.25;      // m (Normal) bzw. dimensionslos
        // Nur fuer die Tabelle: Punkte mit steigendem d, Q3 von 0 bis 1.
        std::vector<SDistributionPoint> points;

        // Prueft die Tabelle: leer = in Ordnung, sonst ein Satz, was fehlt.
        // Zwischen zwei Punkten ist Q3 linear in ln d (die Dichte je ln d
        // also konstant) - wie eine Siebanalyse, ohne Form dazuzuerfinden.
        std::string CheckTable() const
        {
            if (points.size() < 2)
                return "Mindestens zwei Punkte noetig.";
            for (size_t i = 0; i < points.size(); ++i)
            {
                if (!(points[i].size > 0.0))
                    return "Alle Groessen muessen groesser als 0 sein.";
                if (i > 0 && !(points[i].size > points[i - 1].size))
                    return "Jede Groesse nur einmal.";
                if (i > 0 && points[i].fraction < points[i - 1].fraction)
                    return "Q3 darf mit der Groesse nicht abnehmen.";
            }
            if (std::abs(points.front().fraction) > 1e-9)
                return "Der kleinste Punkt braucht Q3 = 0 %.";
            if (std::abs(points.back().fraction - 1.0) > 1e-9)
                return "Der groesste Punkt braucht Q3 = 100 %.";
            return std::string();
        }

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
            case EDistributionType::GGS:
                if (d <= 0.0 || position <= 0.0 || width <= 0.0 || d > position)
                    return 0.0;
                return (width / position) * std::pow(d / position, width - 1.0);
            case EDistributionType::Table:
            {
                if (!CheckTable().empty() || d < points.front().size || d >= points.back().size)
                    return 0.0;
                const size_t i = FindTableInterval(d);
                return (points[i + 1].fraction - points[i].fraction)
                    / (std::log(points[i + 1].size / points[i].size) * d);
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
                return d <= 0.0 ? 0.0 : -std::expm1(-std::pow(d / position, width)); // expm1: genau auch fuer kleine Anteile
            case EDistributionType::GGS:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return d <= 0.0 ? 0.0 : (d >= position ? 1.0 : std::pow(d / position, width));
            case EDistributionType::Table:
            {
                if (!CheckTable().empty() || d <= points.front().size)
                    return 0.0;
                if (d >= points.back().size)
                    return 1.0;
                const size_t i = FindTableInterval(d);
                const SDistributionPoint& a = points[i];
                const SDistributionPoint& b = points[i + 1];
                return a.fraction + (b.fraction - a.fraction) * std::log(d / a.size) / std::log(b.size / a.size);
            }
            case EDistributionType::Normal:
            default:
                if (width <= 0.0)
                    return 0.0;
                return 0.5 * std::erfc(-(d - position) / (width * std::sqrt(2.0)));
            }
        }

        // Umkehrung von Cumulative: Groesse d mit Q3(d) = p (0 < p < 1),
        // analytisch je Typ. Bei der Normalverteilung kann d negativ sein
        // (p < Q3(0)). Ungueltige Parameter ergeben 0.
        double Quantile(double p) const
        {
            switch (type)
            {
            case EDistributionType::LogNormal:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return position * std::exp(width * StandardNormalQuantile(p));
            case EDistributionType::RRSB:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return position * std::pow(-std::log1p(-p), 1.0 / width);
            case EDistributionType::GGS:
                if (position <= 0.0 || width <= 0.0)
                    return 0.0;
                return position * std::pow(std::clamp(p, 0.0, 1.0), 1.0 / width);
            case EDistributionType::Table:
            {
                if (!CheckTable().empty())
                    return 0.0;
                if (p <= 0.0)
                    return points.front().size;
                for (size_t i = 0; i + 1 < points.size(); ++i)
                {
                    const SDistributionPoint& a = points[i];
                    const SDistributionPoint& b = points[i + 1];
                    if (p <= b.fraction && b.fraction > a.fraction)
                        return a.size * std::exp(std::log(b.size / a.size) * (p - a.fraction) / (b.fraction - a.fraction));
                }
                return points.back().size;
            }
            case EDistributionType::Normal:
            default:
                if (width <= 0.0)
                    return 0.0;
                return position + width * StandardNormalQuantile(p);
            }
        }

        // Massenanteil unter sizeMin bzw. ueber sizeMax (exakt aus Q3) - das,
        // was ein Raster von sizeMin bis sizeMax nicht abdecken kann.
        double FractionBelow(double size) const { return Cumulative(size); }
        double FractionAbove(double size) const { return 1.0 - Cumulative(size); }

        // Anteil der Masse, den das Groessenraster erfasst - genau das, was
        // das Inlet einspeist (MassesAtNodes): Q3(d_max) - Q3(d_min). Weicht
        // von 1 ab, wenn die Verteilung ueber den Rasterrand hinausragt.
        double CapturedFraction(const SSizeGrid& sizes) const
        {
            if (!sizes.IsValid())
                return 0.0;
            return Cumulative(sizes.sizeMax) - Cumulative(sizes.sizeMin);
        }

        // Masse je Stuetzstelle (Anteil an der Gesamtmasse), exakt aus Q3:
        // Jede Groesse d gibt ihre Masse anteilig an die beiden benachbarten
        // Stuetzstellen ab, linear nach ihrer Lage dazwischen (in d bzw. ln d)
        // - dieselbe Regel wie fuer Karten (ProjectCards), also der Grenzfall
        // unendlich vieler Karten. Mit der Hutfunktion h_i der Stuetzstelle i
        // ist M_i = Integral h_i dQ3; partiell integriert
        //   M_i = (A_i - A_i-1) / Delta,  A_i = Integral von Q3 ueber [x_i, x_i+1],
        // an den Raendern M_0 = A_0 / Delta - Q3(x_0), M_last = Q3(x_last) - A_last-1 / Delta.
        // Die Summe ist damit exakt Q3(d_max) - Q3(d_min) (die A heben sich
        // weg), auch bei Spruengen der Dichte (GGS bei d_max, Enden der
        // Tabelle), wo die Dichte an den Stuetzstellen mit der Trapezregel
        // daneben laege. A_i per Simpson mit 32 Teilschritten.
        std::vector<double> MassesAtNodes(const SSizeGrid& sizes) const
        {
            std::vector<double> masses(sizes.numClasses, 0.0);
            if (!sizes.IsValid())
                return masses;
            const size_t numNodes = sizes.numClasses;
            const double step = sizes.GetStep();
            const auto cumulativeAt = [&](double position) // position = Stuetzstelle als Kommazahl
            {
                const double size = sizes.IsLogarithmic() ? sizes.sizeMin * std::exp(position * step)
                                                          : sizes.sizeMin + position * step;
                return Cumulative(size);
            };
            const int subSteps = 32;
            std::vector<double> cellIntegral(numNodes - 1); // A_i / Delta = Mittel von Q3 in der Zelle
            for (size_t i = 0; i + 1 < numNodes; ++i)
            {
                double sum = Cumulative(sizes.GetSizeAt(i)) + Cumulative(sizes.GetSizeAt(i + 1));
                for (int k = 1; k < subSteps; ++k)
                    sum += (k % 2 == 1 ? 4.0 : 2.0) * cumulativeAt(static_cast<double>(i) + static_cast<double>(k) / subSteps);
                cellIntegral[i] = sum / (3.0 * subSteps);
            }
            masses[0] = cellIntegral[0] - Cumulative(sizes.GetSizeAt(0));
            for (size_t i = 1; i + 1 < numNodes; ++i)
                masses[i] = cellIntegral[i] - cellIntegral[i - 1];
            masses[numNodes - 1] = Cumulative(sizes.GetSizeAt(numNodes - 1)) - cellIntegral[numNodes - 2];
            return masses;
        }

    private:
        // Tabelle: Abschnitt i mit d_i <= d < d_i+1 (d innerhalb der Punkte).
        size_t FindTableInterval(double d) const
        {
            const auto it = std::upper_bound(points.begin(), points.end(), d,
                [](double value, const SDistributionPoint& point) { return value < point.size; });
            return std::min(static_cast<size_t>(it - points.begin()), points.size() - 1) - 1;
        }
    };
}
