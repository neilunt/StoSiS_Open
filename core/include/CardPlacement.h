#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "CardDensity.h"
#include "SizeDistribution.h"

namespace stosis
{
    // Masse, Anzahl und Kartendichte einer Verteilung als Kurven ueber ln d
    // (Vorschau): Dichten mit der Flaeche 1 an den Mitten feiner Zellen.
    struct SCardDensityCurve
    {
        std::vector<double> sizes;  // m, aufsteigend (geometrische Zellmitten)
        std::vector<double> mass;   // q3*(ln d)
        std::vector<double> number; // q0*(ln d), gezaehlt wie CardNumberWeight
        // Anzahl mit der echten Groesse auch unter countMin, gleich normiert
        // wie number (nur Anzeige; ueber countMin gleich number).
        std::vector<double> numberTrue;
        std::vector<double> cards;  // Kartendichte c(ln d)
    };

    // Raender fuer Kennzahlen und Hinweise: die feinsten 5 % der Partikel und
    // die groebsten 5 % der Masse. Fein zaehlt die Anzahl (dort liegen viele
    // leichte Partikel), grob die Masse (wenige schwere). Bei gleichen
    // Prozenten bekommen beide Raender einer Log-Normal-Verteilung mit
    // f = 0,5 gleich viele Karten.
    inline constexpr double kFineEdgeNumberShare = 0.05;
    inline constexpr double kCoarseEdgeMassShare = 0.05;

    struct SCardPlacement
    {
        std::vector<double> sizes;         // m, aufsteigend
        std::vector<double> massFractions; // Anteil jeder Karte an der Masse, Summe 1
        SCardDensityCurve curve;
        // Schwerste Karte und meiste Partikel je Karte (gezaehlt wie
        // CardNumberWeight), jeweils relativ zum Mittel.
        double maxMassRatio = 0.0;
        double maxNumberRatio = 0.0;
        // Karten unter fineSize (feinste 5 % der Partikel) und ueber
        // coarseSize (groebste 5 % der Masse).
        double fineSize = 0.0;   // m
        double coarseSize = 0.0; // m
        size_t fineCards = 0;
        size_t coarseCards = 0;
    };

    // Karten fuer eine Massenverteilung, z.B. am Inlet (Methoden-Dokument 13.26):
    //  1) Zellen in x = ln d: je 2048 Punkte gleichmaessig in ln d und an
    //     Quantilen der Masse, dazu countMin. Die Masse je Zelle folgt exakt
    //     aus Q3; die Partikelzahl ~ Masse / d^3, unter countMin wie bei
    //     countMin (CardNumberWeight) - sonst waechst sie bei Verteilungen mit
    //     Masse nahe d = 0 ohne Grenze (Normalverteilung, RRSB mit n <= 3), und
    //     abgeschnitten haette die Kartendichte bei countMin einen Sprung.
    //  2) Kartendichte je Zelle c = M(q3*, q0*)^gamma (CardDensity.h).
    //  3) Die Flaeche unter c wird in N gleiche Streifen geschnitten. Jede
    //     Karte sitzt in der Mitte ihres Streifens (Quantil (i - 0,5) / N der
    //     Kartendichte, innerhalb einer Zelle linear in ln d) und traegt die
    //     Masse ihres Streifens, exakt aus Q3. Der erste Streifen reicht bis
    //     d = 0, der letzte bis unendlich: Die Karten tragen genau den ganzen
    //     Massenstrom. Die Auslaeufer ausserhalb der Zellen (je hoechstens
    //     1e-9 der Masse) zaehlen nur fuer die Masse der ersten und letzten
    //     Karte, nicht fuer die Dichte der Randzellen - sonst haette c dort
    //     einen Knick.
    // Bei der Normalverteilung zaehlt nur der Teil mit d > 0 (hochgerechnet auf 1).
    inline SCardPlacement PlaceCardsAtQuantiles(const SSizeDistribution& distribution,
        const SCardSettings& settings, double countMin)
    {
        SCardPlacement placement;
        const size_t numCards = settings.cardsPerSlice;
        if (numCards == 0)
            return placement;

        // Q(d) = Massenanteil unter d (nur d > 0), D(p) = Umkehrung.
        const double below = distribution.Cumulative(0.0); // > 0 nur bei der Normalverteilung
        const double positive = 1.0 - below;
        const auto massBelow = [&](double d)
        {
            return d <= 0.0 ? 0.0 : std::clamp((distribution.Cumulative(d) - below) / positive, 0.0, 1.0);
        };
        const auto sizeAt = [&](double p) { return distribution.Quantile(below + p * positive); };

        const double tail = 1e-9;
        const double bottom = sizeAt(tail);
        const double top = sizeAt(1.0 - tail);
        if (!(positive > 0.0) || !(top > 0.0) || !std::isfinite(top))
            return placement; // ungueltige Verteilung

        // 1) Zellen: unten bis zum Zaehlbereich der Anzahl (sie reicht weiter
        //    ins Feine als die Masse), hoechstens 12 Dekaden unter top.
        double low = bottom > 0.0 ? bottom : top * 1e-12;
        if (countMin > 0.0 && countMin < low)
            low = countMin;
        low = std::max(low, top * 1e-12);
        if (!(low < top))
            low = top * 0.5;
        std::vector<double> x;
        const int kUniform = 2048, kQuantile = 2048;
        x.reserve(kUniform + kQuantile + 4);
        const double x0 = std::log(low), x1 = std::log(top);
        for (int i = 0; i <= kUniform; ++i)
            x.push_back(x0 + (x1 - x0) * static_cast<double>(i) / kUniform);
        for (int i = 0; i <= kQuantile; ++i)
        {
            const double d = sizeAt(tail + (1.0 - 2.0 * tail) * static_cast<double>(i) / kQuantile);
            if (d > low && d < top)
                x.push_back(std::log(d));
        }
        if (countMin > low && countMin < top)
            x.push_back(std::log(countMin));
        // Fast gleiche Punkte zusammenlegen: Eine hauchduenne Zelle am Rand
        // bekaeme sonst den ganzen Auslaeufer und zeigte eine Spitze.
        std::sort(x.begin(), x.end());
        x.erase(std::unique(x.begin(), x.end(), [](double a, double b) { return b - a <= 1e-7; }), x.end());
        if (x.size() < 2)
            return placement;
        const size_t cells = x.size() - 1;
        std::vector<double> cellMass(cells, 0.0), cellNumber(cells, 0.0), cellNumberTrue(cells, 0.0), width(cells, 0.0);
        double previous = massBelow(std::exp(x[0])), totalNumber = 0.0;
        for (size_t k = 0; k < cells; ++k)
        {
            const double da = std::exp(x[k]), db = std::exp(x[k + 1]);
            const double q = std::max(previous, massBelow(db));
            cellMass[k] = q - previous;
            previous = q;
            width[k] = x[k + 1] - x[k];
            cellNumber[k] = CardNumberWeight(cellMass[k], std::sqrt(da * db), countMin);
            cellNumberTrue[k] = CardNumberWeight(cellMass[k], std::sqrt(da * db), 0.0);
            totalNumber += cellNumber[k];
        }

        // 2) Kartendichte je Zelle; ohne gezaehlte Partikel (z.B. Produkt fuer
        //    eine Verteilung ausserhalb des Rasters) nach Masse.
        const double gamma = CardDensityExponent(settings);
        std::vector<double> weight(cells, 0.0);
        double totalWeight = 0.0;
        for (size_t k = 0; k < cells; ++k)
        {
            if (!(width[k] > 0.0))
                continue;
            const double q3 = cellMass[k] / width[k];
            const double q0 = totalNumber > 0.0 ? cellNumber[k] / totalNumber / width[k] : 0.0;
            const double mean = CardDensityMean(q3, q0, settings);
            weight[k] = mean > 0.0 ? std::pow(mean, gamma) * width[k] : 0.0;
            totalWeight += weight[k];
        }
        if (!(totalWeight > 0.0))
        {
            weight = cellMass;
            totalWeight = 0.0;
            for (double w : weight)
                totalWeight += w;
            if (!(totalWeight > 0.0))
                return SCardPlacement();
        }
        std::vector<double> cumulative(cells + 1, 0.0);
        for (size_t k = 0; k < cells; ++k)
            cumulative[k + 1] = cumulative[k] + weight[k] / totalWeight;
        cumulative[cells] = 1.0;
        // x mit dem Kartenanteil p darunter (linear innerhalb einer Zelle).
        const auto positionAt = [&](double p)
        {
            size_t k = static_cast<size_t>(std::lower_bound(cumulative.begin() + 1, cumulative.end(), p)
                - (cumulative.begin() + 1));
            k = std::min(k, cells - 1);
            while (k + 1 < cells && !(weight[k] > 0.0))
                ++k;
            const double span = cumulative[k + 1] - cumulative[k];
            const double t = span > 0.0 ? std::clamp((p - cumulative[k]) / span, 0.0, 1.0) : 0.5;
            return x[k] + t * width[k];
        };

        // 3) N Streifen gleicher Flaeche, Karte in der Mitte, Masse exakt.
        placement.sizes.resize(numCards);
        placement.massFractions.resize(numCards);
        const double n = static_cast<double>(numCards);
        double lowerMass = 0.0;
        for (size_t i = 0; i < numCards; ++i)
        {
            placement.sizes[i] = std::exp(positionAt((static_cast<double>(i) + 0.5) / n));
            const double upperMass = i + 1 == numCards ? 1.0
                : std::max(lowerMass, massBelow(std::exp(positionAt(static_cast<double>(i + 1) / n))));
            placement.massFractions[i] = upperMass - lowerMass;
            lowerMass = upperMass;
        }

        // Kurven fuer die Vorschau.
        SCardDensityCurve& curve = placement.curve;
        curve.sizes.resize(cells);
        curve.mass.resize(cells);
        curve.number.resize(cells);
        curve.numberTrue.resize(cells);
        curve.cards.resize(cells);
        for (size_t k = 0; k < cells; ++k)
        {
            const double w = width[k] > 0.0 ? width[k] : 1.0;
            curve.sizes[k] = std::exp(0.5 * (x[k] + x[k + 1]));
            curve.mass[k] = cellMass[k] / w;
            curve.number[k] = totalNumber > 0.0 ? cellNumber[k] / totalNumber / w : 0.0;
            curve.numberTrue[k] = totalNumber > 0.0 ? cellNumberTrue[k] / totalNumber / w : 0.0;
            curve.cards[k] = weight[k] / totalWeight / w;
        }

        // Kennzahlen.
        double maxMass = 0.0, maxNumber = 0.0, sumNumber = 0.0;
        for (size_t i = 0; i < numCards; ++i)
        {
            const double number = CardNumberWeight(placement.massFractions[i], placement.sizes[i], countMin);
            maxMass = std::max(maxMass, placement.massFractions[i]);
            maxNumber = std::max(maxNumber, number);
            sumNumber += number;
        }
        placement.maxMassRatio = maxMass * n;
        placement.maxNumberRatio = sumNumber > 0.0 ? maxNumber * n / sumNumber : 0.0;
        placement.coarseSize = sizeAt(1.0 - kCoarseEdgeMassShare);
        placement.fineSize = 0.0;
        if (totalNumber > 0.0)
        {
            double sum = 0.0;
            for (size_t k = 0; k < cells; ++k)
            {
                const double share = cellNumber[k] / totalNumber;
                if (sum + share >= kFineEdgeNumberShare && share > 0.0)
                {
                    placement.fineSize = std::exp(x[k] + (kFineEdgeNumberShare - sum) / share * width[k]);
                    break;
                }
                sum += share;
            }
        }
        for (double d : placement.sizes)
        {
            placement.fineCards += d < placement.fineSize ? 1 : 0;
            placement.coarseCards += d > placement.coarseSize ? 1 : 0;
        }
        return placement;
    }
}
