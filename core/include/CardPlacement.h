#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>
#include "CardSettings.h"
#include "SizeDistribution.h"

namespace stosis
{
    // Ein Groessenbereich bei der Platzierung - zum Nachvollziehen und Pruefen.
    struct SCardRange
    {
        int index = 0;
        double lower = 0.0;          // m
        double upper = 0.0;          // m
        double massFraction = 0.0;   // Anteil an der Masse; der erste und letzte Bereich samt Auslaeufer
        double numberFraction = 0.0; // Anteil an der Partikelzahl, gezaehlt in [countMin, countMax]
        bool occupied = false;       // mindestens occupiedThreshold an Masse oder Anzahl
        size_t numCards = 0;         // aus der Aufteilung; 0 = Masse geht an den Nachbarbereich
    };

    struct SCardPlacement
    {
        std::vector<double> sizes;         // m, aufsteigend
        std::vector<double> massFractions; // Anteil jeder Karte an der Masse, Summe 1
        std::vector<SCardRange> ranges;    // alle betrachteten Bereiche, aufsteigend
        size_t numOccupied = 0;            // B_belegt
        size_t minPerRange = 0;            // N_min
    };

    // Karten fuer eine Massenverteilung, z.B. am Inlet (Methoden-Dokument
    // 13.19 Frage A, 13.21):
    //  1) Groessenbereiche [anchor * 2^k, anchor * 2^(k+1)) ueber die ganze
    //     Verteilung; was unter dem ersten bzw. ueber dem letzten Bereich
    //     liegt (Auslaeufer unter 1e-9 der Masse), zaehlt zu diesem.
    //  2) Belegt ist ein Bereich mit mindestens 0,1 % der Masse oder der
    //     Partikelzahl. Die Zahl (Dichte q3/d^3) wird nur in [countMin,
    //     countMax] gezaehlt, sonst waechst sie bei Verteilungen mit Masse
    //     nahe d = 0 (Normalverteilung, RRSB mit n <= 3) ohne Grenze.
    //  3) N_min = ceil(f * N / B_belegt) Karten je belegtem Bereich, der Rest
    //     nach Masse; ganze Karten nach dem groessten Rest, Summe genau N.
    //  4) Bekommt ein Bereich keine Karte, geht seine Masse an den naechsten
    //     Bereich mit Karten darunter (am unteren Ende: darueber) - nichts
    //     geht verloren. Die Karten bleiben in ihrem eigenen Bereich und
    //     werden nur schwerer: So hat jeder belegte Bereich wirklich seine
    //     Mindestzahl, und unter dem Zaehlbereich entstehen keine Karten
    //     (dort wuechse sonst die Partikelzahl ohne Grenze).
    //  5) In jedem Bereich traegt jede Karte dieselbe Masse; ihre Groesse
    //     liegt am Quantil in der Mitte ihres Anteils am Bereich (Quantile von Q3).
    // Bei der Normalverteilung zaehlt nur der Teil mit d > 0 (hochgerechnet
    // auf 1): Die Karten tragen immer genau den ganzen Massenstrom.
    inline SCardPlacement PlaceCardsAtQuantiles(const SSizeDistribution& distribution,
        const SCardSettings& settings, double countMin, double countMax)
    {
        SCardPlacement placement;
        const size_t numCards = settings.cardsPerSlice;
        if (numCards == 0 || !(settings.rangeFactor > 1.0) || !(settings.rangeAnchor > 0.0))
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
        if (!(positive > 0.0) || !(top > 0.0))
            return placement; // ungueltige Verteilung
        const int kMaxRanges = 200;
        const int last = GetCardRangeIndex(top, settings);
        const int first = std::max(bottom > 0.0 ? GetCardRangeIndex(bottom, settings) : last - kMaxRanges,
            last - kMaxRanges);

        // 1) Bereiche: Masse aus Q an den Grenzen, Zahl ~ Integral dQ / d^3
        //    (Mittelpunktregel in Q - auch fuer sehr schmale Verteilungen).
        const size_t numRanges = static_cast<size_t>(last - first + 1);
        std::vector<double> bounds(numRanges + 1); // Q an den Bereichsgrenzen, aussen 0 und 1
        bounds[0] = 0.0;
        bounds[numRanges] = 1.0;
        for (size_t i = 1; i < numRanges; ++i)
            bounds[i] = massBelow(GetCardRangeLower(first + static_cast<int>(i), settings));
        std::vector<SCardRange>& ranges = placement.ranges;
        double totalNumber = 0.0;
        for (size_t i = 0; i < numRanges; ++i)
        {
            SCardRange range;
            range.index = first + static_cast<int>(i);
            range.lower = GetCardRangeLower(range.index, settings);
            range.upper = GetCardRangeLower(range.index + 1, settings);
            range.massFraction = bounds[i + 1] - bounds[i];
            const double a = std::max(range.lower, countMin);
            const double b = std::min(range.upper, countMax);
            if (a < b)
            {
                const double qa = massBelow(a);
                const double qb = massBelow(b);
                const int steps = 64;
                double sum = 0.0;
                for (int s = 0; s < steps; ++s)
                {
                    const double d = sizeAt(qa + (s + 0.5) * (qb - qa) / steps);
                    if (d > 0.0)
                        sum += 1.0 / (d * d * d);
                }
                range.numberFraction = sum * (qb - qa) / steps; // wird unten normiert
                totalNumber += range.numberFraction;
            }
            ranges.push_back(range);
        }

        // 2) Belegte Bereiche.
        size_t numOccupied = 0;
        for (SCardRange& range : ranges)
        {
            if (totalNumber > 0.0)
                range.numberFraction /= totalNumber;
            range.occupied = range.massFraction > 0.0 && (range.massFraction >= settings.occupiedThreshold
                || range.numberFraction >= settings.occupiedThreshold);
            numOccupied += range.occupied ? 1 : 0;
        }
        if (numOccupied == 0)
        {
            std::max_element(ranges.begin(), ranges.end(), [](const SCardRange& x, const SCardRange& y)
                { return x.massFraction < y.massFraction; })->occupied = true;
            numOccupied = 1;
        }

        // 3) Mindestzahl, Rest nach Masse, ganze Karten nach dem groessten Rest.
        size_t minPerRange = static_cast<size_t>(std::ceil(
            settings.reservedFraction * static_cast<double>(numCards) / static_cast<double>(numOccupied) - 1e-9));
        if (minPerRange * numOccupied > numCards)
            minPerRange = numCards / numOccupied;
        const double rest = static_cast<double>(numCards - minPerRange * numOccupied);
        std::vector<double> remainder(numRanges, -1.0);
        size_t assigned = 0;
        for (size_t i = 0; i < numRanges; ++i)
        {
            SCardRange& range = ranges[i];
            const double target = (range.occupied ? static_cast<double>(minPerRange) : 0.0) + rest * range.massFraction;
            range.numCards = static_cast<size_t>(std::floor(target));
            assigned += range.numCards;
            if (range.massFraction > 0.0)
                remainder[i] = target - std::floor(target);
        }
        std::vector<size_t> order(numRanges);
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return remainder[x] > remainder[y]; });
        for (size_t j = 0; assigned < numCards && j < numRanges; ++j)
        {
            if (remainder[order[j]] < 0.0)
                break;
            ++ranges[order[j]].numCards;
            ++assigned;
        }
        placement.numOccupied = numOccupied;
        placement.minPerRange = minPerRange;

        // 4) Masse der kartenlosen Bereiche zum naechsten Bereich mit Karten.
        std::vector<double> groupMass(numRanges, 0.0);
        double pending = 0.0; // kartenlose Bereiche unter dem ersten mit Karten
        size_t previous = numRanges;
        for (size_t i = 0; i < numRanges; ++i)
        {
            if (ranges[i].numCards > 0)
            {
                groupMass[i] = ranges[i].massFraction + pending;
                pending = 0.0;
                previous = i;
            }
            else if (previous < numRanges)
                groupMass[previous] += ranges[i].massFraction;
            else
                pending += ranges[i].massFraction;
        }

        // 5) Karten an den Quantilen des eigenen Bereichs, gleiche Masse.
        placement.sizes.reserve(assigned);
        placement.massFractions.reserve(assigned);
        for (size_t i = 0; i < numRanges; ++i)
        {
            const size_t n = ranges[i].numCards;
            if (n == 0)
                continue;
            const double qa = massBelow(ranges[i].lower);
            const double qb = massBelow(ranges[i].upper);
            const double step = (qb - qa) / static_cast<double>(n);
            for (size_t c = 0; c < n; ++c)
            {
                placement.sizes.push_back(sizeAt(qa + (static_cast<double>(c) + 0.5) * step));
                placement.massFractions.push_back(groupMass[i] / static_cast<double>(n));
            }
        }
        return placement;
    }
}
