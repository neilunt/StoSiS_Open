#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>
#include "CardSettings.h"

namespace stosis
{
    // Kartendichte (Methodenhandbuch 9.3): Wo Karten sitzen, legt ein
    // Mittel aus Masse und Partikelzahl fest, abgeflacht mit dem Exponenten
    // gamma:
    //   c ~ M(q3*, q0*)^gamma mit M(a, b) = (1 - f) a + f b             (Summe)
    //                                       a^(1 - f) b^f                (Produkt)
    //                                       ((1 - f) a^2 + f b^2)^(1/2)  (Quadratisch).
    // Jedes M ist homogen vom Grad 1, M(k a, k b) = k M(a, b): Halbiert man
    // eine Karte, traegt jede Haelfte genau den halben Anteil.

    inline double CardDensityMean(double mass, double number, const SCardSettings& settings)
    {
        const double f = std::clamp(settings.numberShare, 0.0, 1.0);
        mass = std::max(mass, 0.0);
        number = std::max(number, 0.0);
        switch (settings.rule)
        {
        case ECardDensityRule::Product:
            return std::pow(mass, 1.0 - f) * std::pow(number, f);
        case ECardDensityRule::Quadratic:
            return std::sqrt((1.0 - f) * mass * mass + f * number * number);
        case ECardDensityRule::Sum:
        default:
            return (1.0 - f) * mass + f * number;
        }
    }

    // Exponent gamma der Abflachung, begrenzt auf [0,1; 1].
    inline double CardDensityExponent(const SCardSettings& settings)
    {
        return std::clamp(settings.exponent, 0.1, 1.0);
    }

    // Gezaehlte Partikelzahl einer Masse bei der Groesse d (gleiche Dichte,
    // beliebige Einheit): Masse / d^3, unter countMin so, als laegen die
    // Partikel bei countMin - so sieht sie auch das Raster, und die Zahl
    // waechst bei Verteilungen mit Masse nahe d = 0 (Normalverteilung, RRSB
    // mit n <= 3) nicht ohne Grenze.
    inline double CardNumberWeight(double mass, double size, double countMin)
    {
        const double d = std::max(size, countMin);
        return d > 0.0 ? std::max(mass, 0.0) / (d * d * d) : 0.0;
    }

    // Anteil jeder Karte an der Kartendichte einer Stromscheibe (Summe 1),
    // fuer die Grenzen je Stromscheibe (CardLimits.h) und die Kartenansicht.
    // sizes in m, masses in beliebiger Einheit (z.B. kg im Paket); Partikelzahl wie
    // am Inlet (CardNumberWeight). Je Karte a_i = M(m_i / Summe m, n_i /
    // Summe n). Abflachung (gamma < 1): w_i = a_i * rho^(gamma - 1) mit der
    // Dichte rho = Summe a an der Groesse der Karte / Breite ihrer Zelle in
    // ln d (halbe Wege zu den Nachbargroessen; Karten gleicher Groesse teilen
    // sich ihre Zelle nach a). Am Inlet traegt so jede Karte etwa 1/N.
    inline std::vector<double> ComputeCardShares(const std::vector<double>& sizes, const std::vector<double>& masses,
        const SCardSettings& settings, double countMin)
    {
        const size_t n = std::min(sizes.size(), masses.size());
        std::vector<double> shares(n, 0.0);
        std::vector<double> number(n, 0.0);
        double totalMass = 0.0, totalNumber = 0.0;
        for (size_t k = 0; k < n; ++k)
        {
            totalMass += std::max(masses[k], 0.0);
            number[k] = CardNumberWeight(masses[k], sizes[k], countMin);
            totalNumber += number[k];
        }
        if (!(totalMass > 0.0))
            return shares;
        std::vector<double> weight(n);
        for (size_t k = 0; k < n; ++k)
            weight[k] = CardDensityMean(std::max(masses[k], 0.0) / totalMass,
                totalNumber > 0.0 ? number[k] / totalNumber : 0.0, settings);

        const double gamma = CardDensityExponent(settings);
        if (gamma < 1.0 && n > 1)
        {
            std::vector<size_t> order(n);
            std::iota(order.begin(), order.end(), size_t(0));
            std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return sizes[x] < sizes[y]; });
            // Gruppen gleicher Groesse: erste Stelle in order, ln d, Summe a.
            std::vector<size_t> first;
            std::vector<double> position, amount;
            for (size_t j = 0; j < n; ++j)
            {
                const size_t k = order[j];
                if (first.empty() || sizes[k] != sizes[order[first.back()]])
                {
                    first.push_back(j);
                    position.push_back(std::log(std::max(sizes[k], 1e-300)));
                    amount.push_back(0.0);
                }
                amount.back() += weight[k];
            }
            first.push_back(n);
            const size_t groups = position.size();
            std::vector<double> flattened(n, 0.0);
            for (size_t g = 0; g < groups; ++g)
            {
                double width = 1.0;
                if (groups > 1)
                    width = g == 0 ? position[1] - position[0]
                        : g + 1 == groups ? position[g] - position[g - 1]
                        : 0.5 * (position[g + 1] - position[g - 1]);
                const double density = amount[g] / std::max(width, 1e-300);
                for (size_t j = first[g]; j < first[g + 1]; ++j)
                {
                    const size_t k = order[j];
                    flattened[k] = density > 0.0 ? weight[k] * std::pow(density, gamma - 1.0) : 0.0;
                }
            }
            weight.swap(flattened);
        }
        const double total = std::accumulate(weight.begin(), weight.end(), 0.0);
        if (total > 0.0)
            for (size_t k = 0; k < n; ++k)
                shares[k] = weight[k] / total;
        return shares;
    }
}
