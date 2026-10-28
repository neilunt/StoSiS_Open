#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>
#include "CardSettings.h"

namespace stosis
{
    // Verteilungen direkt aus den Karten eines Zeitpunkts (Kartenansicht,
    // Methoden-Dokument 13.23). Feine Klassen: jeder Groessenbereich des
    // Siebsatzes wird in perRange gleich breite Teile in ln d geteilt.
    // Dichte q3*(ln d) = Massenanteil der Klasse / Klassenbreite in ln d;
    // damit sind Klassen verschiedener Breite und die Kurve vom
    // Groessenraster untereinander vergleichbar.

    struct SCardClass
    {
        double lower = 0.0;     // m
        double upper = 0.0;     // m
        size_t cards = 0;
        double massShare = 0.0; // Anteil an der Masse aller Karten

        double GetDensity() const
        {
            return (lower > 0.0 && upper > lower) ? massShare / std::log(upper / lower) : 0.0;
        }
    };

    // Grenzen der feinen Klassen von Bereich firstRange bis lastRange (beide
    // eingeschlossen), aufsteigend; die Bereichsgrenzen selbst sind dabei.
    inline std::vector<double> MakeFineClassBounds(int firstRange, int lastRange, int perRange,
                                                   const SCardSettings& settings)
    {
        perRange = std::max(perRange, 1);
        std::vector<double> bounds;
        for (int k = firstRange; k <= lastRange; ++k)
        {
            const double lower = GetCardRangeLower(k, settings);
            for (int j = 0; j < perRange; ++j)
                bounds.push_back(lower * std::pow(settings.rangeFactor, static_cast<double>(j) / perRange));
        }
        bounds.push_back(GetCardRangeLower(lastRange + 1, settings));
        return bounds;
    }

    // Karten nach Groesse in die Klassen zwischen bounds einsortieren; Karten
    // ausserhalb zaehlen zur ersten bzw. letzten Klasse.
    inline std::vector<SCardClass> BinCards(const std::vector<double>& sizes, const std::vector<double>& masses,
                                            const std::vector<double>& bounds)
    {
        std::vector<SCardClass> classes(bounds.size() > 1 ? bounds.size() - 1 : 0);
        for (size_t i = 0; i < classes.size(); ++i)
        {
            classes[i].lower = bounds[i];
            classes[i].upper = bounds[i + 1];
        }
        if (classes.empty())
            return classes;
        const size_t n = std::min(sizes.size(), masses.size());
        const double total = std::accumulate(masses.begin(), masses.begin() + static_cast<std::ptrdiff_t>(n), 0.0);
        for (size_t k = 0; k < n; ++k)
        {
            const size_t above = static_cast<size_t>(std::upper_bound(bounds.begin(), bounds.end(), sizes[k]) - bounds.begin());
            SCardClass& c = classes[std::min(classes.size() - 1, above > 0 ? above - 1 : size_t(0))];
            ++c.cards;
            if (total > 0.0)
                c.massShare += masses[k] / total;
        }
        return classes;
    }

    // Summenkurve Q3 aus den Karten als Treppe: Groessen aufsteigend,
    // q3[k] = Masse der Karten 0..k / Masse aller Karten.
    struct SCardCumulative
    {
        std::vector<double> sizes; // m
        std::vector<double> q3;
    };

    inline SCardCumulative MakeCardCumulative(const std::vector<double>& sizes, const std::vector<double>& masses)
    {
        const size_t n = std::min(sizes.size(), masses.size());
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sizes[a] < sizes[b]; });
        const double total = std::accumulate(masses.begin(), masses.begin() + static_cast<std::ptrdiff_t>(n), 0.0);
        SCardCumulative result;
        result.sizes.resize(n);
        result.q3.resize(n);
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            sum += masses[order[i]];
            result.sizes[i] = sizes[order[i]];
            result.q3[i] = total > 0.0 ? sum / total : 0.0;
        }
        return result;
    }

    // Kleinste Kartengroesse, bei der Q3 den Anteil fraction erreicht
    // (d10, d50, d90); 0 ohne Karten.
    inline double GetCardQuantile(const SCardCumulative& cumulative, double fraction)
    {
        if (cumulative.sizes.empty())
            return 0.0;
        const auto it = std::lower_bound(cumulative.q3.begin(), cumulative.q3.end(), fraction - 1e-12);
        return it == cumulative.q3.end() ? cumulative.sizes.back()
                                         : cumulative.sizes[static_cast<size_t>(it - cumulative.q3.begin())];
    }

    // Summenkurve einer Dichte q*(ln d) an Stuetzstellen (Groessen > 0,
    // aufsteigend): Trapezregel in ln d, am Ende auf 1 normiert.
    inline std::vector<double> CumulativeFromDensity(const std::vector<double>& sizes, const std::vector<double>& density)
    {
        const size_t n = std::min(sizes.size(), density.size());
        std::vector<double> q(n, 0.0);
        for (size_t i = 1; i < n; ++i)
            q[i] = q[i - 1] + 0.5 * (density[i - 1] + density[i]) * std::log(sizes[i] / sizes[i - 1]);
        const double total = n > 0 ? q.back() : 0.0;
        if (total > 0.0)
            for (double& v : q)
                v /= total;
        return q;
    }

    // Groesse, bei der eine stetige Summenkurve (CumulativeFromDensity) den
    // Anteil fraction erreicht, linear in ln d zwischen den Stuetzstellen.
    inline double GetCurveQuantile(const std::vector<double>& sizes, const std::vector<double>& cumulative, double fraction)
    {
        const size_t n = std::min(sizes.size(), cumulative.size());
        if (n == 0)
            return 0.0;
        const size_t i = static_cast<size_t>(std::lower_bound(cumulative.begin(), cumulative.begin()
            + static_cast<std::ptrdiff_t>(n), fraction) - cumulative.begin());
        if (i == 0)
            return sizes[0];
        if (i >= n)
            return sizes[n - 1];
        const double span = cumulative[i] - cumulative[i - 1];
        const double f = span > 0.0 ? (fraction - cumulative[i - 1]) / span : 0.0;
        return sizes[i - 1] * std::pow(sizes[i] / sizes[i - 1], f);
    }
}
