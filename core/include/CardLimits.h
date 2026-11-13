#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>
#include "CardDensity.h"
#include "StreamSlice.h"

namespace stosis
{
    // Grenzen je Stromscheibe (Methodenhandbuch 9.5).
    // Jede Karte traegt einen Anteil s an der Kartendichte
    // ihrer Scheibe (ComputeCardShares, dieselbe Regel wie am Inlet); Bezug
    // ist N = Karten je Stromscheibe.
    //  - Mehr als upperFactor x N Karten: zusammenlegen. Immer zwei der
    //    Groesse nach benachbarte Karten derselben Stoffkombination, die
    //    leichtesten Paare nach Anteil zuerst (so trifft es nicht die Karten,
    //    die die Kartendichte an den Raendern absichtlich leicht macht), bei
    //    gleichem Anteil das Paar mit dem kleinsten Groessenabstand; ein Paar
    //    traegt zusammen hoechstens splitFactor / N. Masse und Partikelzahl
    //    bleiben erhalten, die Groesse folgt daraus: d^3 = (m1 + m2) /
    //    (m1 / d1^3 + m2 / d2^3) - genau fuer Karten aus einem Stoff; bei
    //    gemischten Karten zaehlt die Masse statt des Volumens.
    //  - Eine Karte mit s > splitFactor / N: teilen in k = ceil(s N /
    //    splitFactor) gleiche Stuecke. Groesse und Zusammensetzung bleiben,
    //    die Umrechnung aufs Raster aendert sich nicht.
    // Zwei gleiche Zulaeufe am Merger ergeben 2N Karten mit je etwa 1/(2N):
    // gerade noch innerhalb. Scheiben innerhalb der Grenzen bleiben unveraendert.

    // Was ApplySliceLimits an einer Scheibe geaendert hat.
    struct SLimitCounts
    {
        size_t added = 0;   // durch Teilen dazugekommene Karten
        size_t removed = 0; // durch Zusammenlegen weggefallene Karten
    };

    // Grenzen auf eine Stromscheibe anwenden; countMin wie bei der Platzierung
    // am Inlet (Partikel kleinerer Karten zaehlen wie bei countMin).
    inline SLimitCounts ApplySliceLimits(CStreamSlice& slice, const SCardSettings& settings, double countMin)
    {
        SLimitCounts counts;
        const size_t n = slice.GetNumCards();
        if (n == 0 || !slice.HasCards() || settings.cardsPerSlice == 0)
            return counts;
        const double numCards = static_cast<double>(settings.cardsPerSlice);
        const size_t limit = std::max<size_t>(1, static_cast<size_t>(std::floor(std::max(settings.upperFactor, 1.0) * numCards + 1e-9)));
        const double heaviest = std::max(settings.splitFactor, 1.0) / numCards;
        const size_t numC = slice.GetNumCompounds();

        // Arbeitskopie: Groesse, Masse, Stoffkombination (Bit je Stoff), Masse je Stoff.
        std::vector<double> size(n), mass(n, 0.0), flow(n * numC);
        std::vector<uint64_t> key(n, 0);
        for (size_t k = 0; k < n; ++k)
        {
            size[k] = slice.GetCardSize(k);
            for (size_t c = 0; c < numC; ++c)
            {
                const double f = slice.GetCardMass(k, c);
                flow[k * numC + c] = f;
                mass[k] += f;
                if (f > 0.0)
                    key[k] |= uint64_t(1) << (c % 64);
            }
        }
        std::vector<double> share = ComputeCardShares(size, mass, settings, countMin);
        const auto anyHeavy = [&]()
        {
            return std::any_of(share.begin(), share.end(), [&](double s) { return s > heaviest; });
        };
        if (size.size() <= limit && !anyHeavy())
            return counts;

        // 1) Zusammenlegen, bis hoechstens limit Karten bleiben (oder kein Paar mehr passt).
        struct SPair
        {
            double share;
            double gap;
            size_t at;
        };
        while (size.size() > limit)
        {
            const size_t m = size.size();
            std::vector<size_t> order(m);
            std::iota(order.begin(), order.end(), size_t(0));
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                { return key[a] != key[b] ? key[a] < key[b] : size[a] < size[b]; });
            std::vector<SPair> pairs;
            pairs.reserve(m);
            for (size_t j = 0; j + 1 < m; ++j)
            {
                const size_t a = order[j], b = order[j + 1];
                if (key[a] == key[b] && share[a] + share[b] <= heaviest)
                    pairs.push_back({ share[a] + share[b],
                        std::abs(std::log(std::max(size[b], 1e-30) / std::max(size[a], 1e-30))), j });
            }
            std::stable_sort(pairs.begin(), pairs.end(), [](const SPair& x, const SPair& y)
                { return x.share != y.share ? x.share < y.share : x.gap < y.gap; });
            const size_t need = m - limit;
            std::vector<char> used(m, 0), dead(m, 0);
            size_t done = 0;
            for (const SPair& pair : pairs)
            {
                if (done == need)
                    break;
                if (used[pair.at] || used[pair.at + 1])
                    continue;
                const size_t a = order[pair.at], b = order[pair.at + 1];
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
            // Uebrige Karten in ihrer Reihenfolge behalten, Anteile neu.
            size_t w = 0;
            for (size_t k = 0; k < m; ++k)
            {
                if (dead[k])
                    continue;
                size[w] = size[k];
                mass[w] = mass[k];
                key[w] = key[k];
                for (size_t c = 0; c < numC; ++c)
                    flow[w * numC + c] = flow[k * numC + c];
                ++w;
            }
            size.resize(w);
            mass.resize(w);
            key.resize(w);
            flow.resize(w * numC);
            counts.removed += done;
            share = ComputeCardShares(size, mass, settings, countMin);
        }

        // 2) Teilen: zu schwere Karten in gleiche Stuecke.
        std::vector<double> newSizes, newFlows;
        newSizes.reserve(size.size());
        newFlows.reserve(size.size() * numC);
        for (size_t k = 0; k < size.size(); ++k)
        {
            const size_t pieces = share[k] > heaviest
                ? static_cast<size_t>(std::ceil(share[k] / heaviest - 1e-12)) : size_t(1);
            const double part = 1.0 / static_cast<double>(pieces);
            for (size_t q = 0; q < pieces; ++q)
            {
                newSizes.push_back(size[k]);
                for (size_t c = 0; c < numC; ++c)
                    newFlows.push_back(flow[k * numC + c] * part);
            }
            counts.added += pieces - 1;
        }
        slice.SetCards(std::move(newSizes), std::move(newFlows));
        return counts;
    }
}
