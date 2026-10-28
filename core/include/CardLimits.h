#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>
#include "CardSettings.h"
#include "StreamSlice.h"

namespace stosis
{
    // Grenzen je Stromscheibe (Partikelmodus Schritt 2.4, Methoden-Dokument
    // 13.19 und 13.24): Jeder Groessenbereich des Siebsatzes soll in jeder
    // Stromscheibe zwischen einer Unter- und einer Obergrenze an Karten
    // liegen. Bezug ist N = Karten je Stromscheibe; Mindest- und Sollzahl
    // wie am Inlet (CardPlacement.h):
    //   N_min      = ceil(f * N / B_belegt) je belegtem Bereich,
    //   Sollzahl   = N_min (wenn belegt) + (N - B_belegt * N_min) * Massenanteil,
    //   Obergrenze = upperFactor * Sollzahl (aufgerundet, mindestens 1).
    // Belegt ist ein Bereich mit mindestens 0,1 % der Masse oder der
    // Partikelzahl der Scheibe (Zahl ~ Masse / d^3 bei gleicher Dichte).
    //  - Zu wenige Karten: teilen. Jede Karte des Bereichs wird in ganze
    //    Stuecke gleicher Masse zerlegt, die Stueckzahl richtet sich nach
    //    ihrer Masse (mindestens 1, zusaetzliche Stuecke systematisch ueber
    //    den Bereich verteilt). Groesse und Zusammensetzung bleiben, die
    //    Umrechnung aufs Raster aendert sich nicht.
    //  - Zu viele Karten: zusammenlegen. Immer zwei der Groesse nach
    //    benachbarte Karten derselben Stoffkombination im selben Bereich, die
    //    leichtesten Paare zuerst (so bleiben die Massen je Karte aehnlich wie
    //    am Inlet), bei gleicher Masse das Paar mit dem kleinsten
    //    Groessenabstand. Masse und Partikelzahl bleiben erhalten, die Groesse
    //    folgt daraus: d^3 = (m1 + m2) / (m1 / d1^3 + m2 / d2^3) - genau fuer
    //    Karten aus einem Stoff; bei gemischten Karten zaehlt die Masse statt
    //    des Volumens. Die neue Groesse liegt zwischen d1 und d2, also im
    //    selben Bereich.
    // Geteilt bzw. zusammengelegt wird nur, bis die Grenze erreicht ist;
    // Scheiben innerhalb der Grenzen bleiben unveraendert.

    struct SSliceRange
    {
        int index = 0;
        double lower = 0.0;          // m
        double upper = 0.0;          // m
        double massFraction = 0.0;   // Anteil an der Masse der Scheibe
        double numberFraction = 0.0; // Anteil an der Partikelzahl (Masse / d^3)
        bool occupied = false;
        size_t cards = 0;            // Karten in der Scheibe
        double target = 0.0;         // Sollzahl
        size_t minCards = 0;         // Untergrenze: N_min, 0 wenn nicht belegt
        size_t maxCards = 0;         // Obergrenze
    };

    struct SSliceLimits
    {
        std::vector<SSliceRange> ranges; // Bereiche mit Karten, aufsteigend
        size_t numOccupied = 0;          // B_belegt
        size_t minPerRange = 0;          // N_min
    };

    // Was ApplySliceLimits an einer Scheibe geaendert hat.
    struct SLimitCounts
    {
        size_t added = 0;   // durch Teilen dazugekommene Karten
        size_t removed = 0; // durch Zusammenlegen weggefallene Karten
    };

    namespace detail
    {
        // Grenzen der Scheibe; cardRange bekommt je Karte die Stelle ihres
        // Bereichs in SSliceLimits::ranges.
        inline SSliceLimits ComputeSliceLimits(const std::vector<double>& sizes, const std::vector<double>& masses,
            const SCardSettings& settings, std::vector<size_t>* cardRange)
        {
            SSliceLimits limits;
            const size_t n = std::min(sizes.size(), masses.size());
            if (n == 0 || settings.cardsPerSlice == 0 || !(settings.rangeFactor > 1.0) || !(settings.rangeAnchor > 0.0))
                return limits;
            std::vector<int> index(n);
            for (size_t k = 0; k < n; ++k)
                index[k] = GetCardRangeIndex(std::max(sizes[k], 1e-30), settings);
            const auto [lowest, highest] = std::minmax_element(index.begin(), index.end());
            const int first = *lowest;
            std::vector<SSliceRange> all(static_cast<size_t>(*highest - first + 1));
            double totalMass = 0.0, totalNumber = 0.0;
            for (size_t k = 0; k < n; ++k)
            {
                SSliceRange& range = all[static_cast<size_t>(index[k] - first)];
                const double d = std::max(sizes[k], 1e-30);
                const double m = std::max(masses[k], 0.0);
                ++range.cards;
                range.massFraction += m;
                range.numberFraction += m / (d * d * d);
                totalMass += m;
                totalNumber += m / (d * d * d);
            }
            if (!(totalMass > 0.0))
                return limits;

            // Belegte Bereiche wie am Inlet.
            size_t numOccupied = 0;
            for (size_t i = 0; i < all.size(); ++i)
            {
                SSliceRange& range = all[i];
                if (range.cards == 0)
                    continue;
                range.index = first + static_cast<int>(i);
                range.lower = GetCardRangeLower(range.index, settings);
                range.upper = GetCardRangeLower(range.index + 1, settings);
                range.massFraction /= totalMass;
                range.numberFraction = totalNumber > 0.0 ? range.numberFraction / totalNumber : 0.0;
                range.occupied = range.massFraction > 0.0 && (range.massFraction >= settings.occupiedThreshold
                    || range.numberFraction >= settings.occupiedThreshold);
                numOccupied += range.occupied ? 1 : 0;
            }
            if (numOccupied == 0)
            {
                std::max_element(all.begin(), all.end(), [](const SSliceRange& x, const SSliceRange& y)
                    { return x.massFraction < y.massFraction; })->occupied = true;
                numOccupied = 1;
            }

            // Mindestzahl, Sollzahl, Obergrenze.
            const size_t numCards = settings.cardsPerSlice;
            size_t minPerRange = static_cast<size_t>(std::ceil(
                settings.reservedFraction * static_cast<double>(numCards) / static_cast<double>(numOccupied) - 1e-9));
            if (minPerRange * numOccupied > numCards)
                minPerRange = numCards / numOccupied;
            const double rest = static_cast<double>(numCards - minPerRange * numOccupied);
            std::vector<size_t> position(all.size(), 0);
            for (size_t i = 0; i < all.size(); ++i)
            {
                SSliceRange& range = all[i];
                if (range.cards == 0)
                    continue;
                range.minCards = range.occupied ? minPerRange : 0;
                range.target = static_cast<double>(range.minCards) + rest * range.massFraction;
                const size_t upper = static_cast<size_t>(std::ceil(settings.upperFactor * range.target - 1e-9));
                range.maxCards = std::max({ range.minCards, size_t(1), upper });
                position[i] = limits.ranges.size();
                limits.ranges.push_back(range);
            }
            limits.numOccupied = numOccupied;
            limits.minPerRange = minPerRange;
            if (cardRange)
            {
                cardRange->resize(n);
                for (size_t k = 0; k < n; ++k)
                    (*cardRange)[k] = position[static_cast<size_t>(index[k] - first)];
            }
            return limits;
        }

        // Karten eines Bereichs in Stuecke teilen, bis es target Karten sind.
        // Jede Karte behaelt mindestens ein Stueck; die zusaetzlichen Stuecke
        // gehen nach Masse und werden der Groesse nach systematisch verteilt
        // (aufsummiert und gerundet, wie beim Konto-Verfahren des Splitters).
        // Bei gleich schweren Karten wird so z.B. jede zweite geteilt, ueber
        // den ganzen Bereich - nicht nur die kleinsten.
        inline void SplitRange(const CStreamSlice& slice, std::vector<size_t> cards,
            const std::vector<double>& masses, size_t target, std::vector<double>& sizes, std::vector<double>& flows)
        {
            const size_t m = cards.size();
            const size_t numC = slice.GetNumCompounds();
            std::stable_sort(cards.begin(), cards.end(),
                [&](size_t a, size_t b) { return slice.GetCardSize(a) < slice.GetCardSize(b); });
            double total = 0.0;
            for (size_t k : cards)
                total += std::max(masses[k], 0.0);
            // Wunsch je Karte ueber das erste Stueck hinaus; die Summe ist
            // mindestens extra und wird auf extra skaliert.
            const size_t extra = target > m ? target - m : 0;
            std::vector<double> want(m);
            double wantSum = 0.0;
            for (size_t i = 0; i < m; ++i)
            {
                const double share = total > 0.0 ? std::max(masses[cards[i]], 0.0) / total : 1.0 / static_cast<double>(m);
                want[i] = std::max(0.0, static_cast<double>(target) * share - 1.0);
                wantSum += want[i];
            }
            if (!(wantSum > 0.0))
            {
                std::fill(want.begin(), want.end(), 1.0);
                wantSum = static_cast<double>(m);
            }
            double running = 0.0;
            size_t given = 0;
            for (size_t i = 0; i < m; ++i)
            {
                running += want[i] * static_cast<double>(extra) / wantSum;
                const size_t upTo = (i + 1 == m) ? extra
                    : std::min(extra, static_cast<size_t>(std::floor(running + 0.5)));
                const size_t pieces = 1 + (upTo > given ? upTo - given : 0);
                given = std::max(given, upTo);
                const size_t k = cards[i];
                const double share = 1.0 / static_cast<double>(pieces);
                for (size_t q = 0; q < pieces; ++q)
                {
                    sizes.push_back(slice.GetCardSize(k));
                    for (size_t c = 0; c < numC; ++c)
                        flows.push_back(slice.GetCardMassFlow(k, c) * share);
                }
            }
        }

        // Karten eines Bereichs paarweise zusammenlegen, bis es hoechstens
        // limit Karten sind (oder nur noch verschiedene Stoffkombinationen
        // uebrig sind).
        inline void MergeRange(const CStreamSlice& slice, const std::vector<size_t>& cards, size_t limit,
            std::vector<double>& sizes, std::vector<double>& flows)
        {
            const size_t m = cards.size();
            const size_t numC = slice.GetNumCompounds();
            // Arbeitskopie: Groesse, Masse, Stoffkombination (Bit je Stoff), Massenstrom je Stoff.
            std::vector<double> size(m), mass(m, 0.0), flow(m * numC);
            std::vector<uint64_t> key(m, 0);
            for (size_t i = 0; i < m; ++i)
            {
                const size_t k = cards[i];
                size[i] = slice.GetCardSize(k);
                for (size_t c = 0; c < numC; ++c)
                {
                    const double f = slice.GetCardMassFlow(k, c);
                    flow[i * numC + c] = f;
                    mass[i] += f;
                    if (f > 0.0)
                        key[i] |= uint64_t(1) << (c % 64);
                }
            }
            struct SPair
            {
                double mass;
                double gap;
                size_t at;
            };
            std::vector<size_t> alive(m);
            std::iota(alive.begin(), alive.end(), size_t(0));
            std::vector<char> dead(m, 0);
            while (alive.size() > limit)
            {
                // Nach Stoffkombination, dann Groesse; Paare benachbarter Karten
                // derselben Kombination, die leichtesten zuerst.
                std::stable_sort(alive.begin(), alive.end(), [&](size_t a, size_t b)
                    { return key[a] != key[b] ? key[a] < key[b] : size[a] < size[b]; });
                std::vector<SPair> pairs;
                pairs.reserve(alive.size());
                for (size_t j = 0; j + 1 < alive.size(); ++j)
                {
                    const size_t a = alive[j], b = alive[j + 1];
                    if (key[a] == key[b])
                        pairs.push_back({ mass[a] + mass[b],
                            std::abs(std::log(std::max(size[b], 1e-30) / std::max(size[a], 1e-30))), j });
                }
                std::stable_sort(pairs.begin(), pairs.end(), [](const SPair& x, const SPair& y)
                    { return x.mass != y.mass ? x.mass < y.mass : x.gap < y.gap; });
                const size_t need = alive.size() - limit;
                std::vector<char> used(alive.size(), 0);
                size_t done = 0;
                for (const SPair& pair : pairs)
                {
                    if (done == need)
                        break;
                    if (used[pair.at] || used[pair.at + 1])
                        continue;
                    const size_t a = alive[pair.at], b = alive[pair.at + 1];
                    const double ma = mass[a], mb = mass[b];
                    if (ma > 0.0 && mb > 0.0)
                    {
                        const double da = size[a], db = size[b];
                        size[a] = std::cbrt((ma + mb) / (ma / (da * da * da) + mb / (db * db * db)));
                    }
                    else if (mb > 0.0)
                        size[a] = size[b];
                    mass[a] = ma + mb;
                    for (size_t c = 0; c < numC; ++c)
                        flow[a * numC + c] += flow[b * numC + c];
                    used[pair.at] = used[pair.at + 1] = 1;
                    dead[b] = 1;
                    ++done;
                }
                if (done == 0)
                    break;
                alive.erase(std::remove_if(alive.begin(), alive.end(), [&](size_t i) { return dead[i] != 0; }), alive.end());
            }
            for (size_t i : alive)
            {
                sizes.push_back(size[i]);
                for (size_t c = 0; c < numC; ++c)
                    flows.push_back(flow[i * numC + c]);
            }
        }
    }

    // Grenzen fuer Karten mit diesen Groessen [m] und Massen (beliebige
    // Einheit, z.B. Massenstrom je Karte) - fuer Anzeige und Tests.
    inline SSliceLimits ComputeSliceLimits(const std::vector<double>& sizes, const std::vector<double>& masses,
        const SCardSettings& settings)
    {
        return detail::ComputeSliceLimits(sizes, masses, settings, nullptr);
    }

    // Grenzen auf eine Stromscheibe anwenden: teilen bzw. zusammenlegen, wo ein
    // Bereich ausserhalb liegt. Die Karten stehen danach nach Bereichen
    // geordnet; liegt alles innerhalb, bleibt die Scheibe unveraendert.
    inline SLimitCounts ApplySliceLimits(CStreamSlice& slice, const SCardSettings& settings)
    {
        SLimitCounts counts;
        const size_t n = slice.GetNumCards();
        if (n == 0 || !slice.HasCards())
            return counts;
        std::vector<double> sizes(n), masses(n);
        for (size_t k = 0; k < n; ++k)
        {
            sizes[k] = slice.GetCardSize(k);
            masses[k] = slice.GetCardMassFlow(k);
        }
        std::vector<size_t> cardRange;
        const SSliceLimits limits = detail::ComputeSliceLimits(sizes, masses, settings, &cardRange);
        bool outside = false;
        for (const SSliceRange& range : limits.ranges)
            outside = outside || range.cards < range.minCards || range.cards > range.maxCards;
        if (!outside)
            return counts;

        std::vector<std::vector<size_t>> members(limits.ranges.size());
        for (size_t k = 0; k < n; ++k)
            members[cardRange[k]].push_back(k);
        const size_t numC = slice.GetNumCompounds();
        std::vector<double> newSizes, newFlows;
        newSizes.reserve(n);
        newFlows.reserve(n * numC);
        for (size_t r = 0; r < limits.ranges.size(); ++r)
        {
            const SSliceRange& range = limits.ranges[r];
            const std::vector<size_t>& cards = members[r];
            const size_t before = newSizes.size();
            if (range.cards < range.minCards)
                detail::SplitRange(slice, cards, masses, range.minCards, newSizes, newFlows);
            else if (range.cards > range.maxCards)
                detail::MergeRange(slice, cards, range.maxCards, newSizes, newFlows);
            else
                for (size_t k : cards)
                {
                    newSizes.push_back(slice.GetCardSize(k));
                    for (size_t c = 0; c < numC; ++c)
                        newFlows.push_back(slice.GetCardMassFlow(k, c));
                }
            const size_t after = newSizes.size() - before;
            if (after > cards.size())
                counts.added += after - cards.size();
            else
                counts.removed += cards.size() - after;
        }
        slice.SetCards(std::move(newSizes), std::move(newFlows));
        return counts;
    }
}
