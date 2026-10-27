#pragma once
#include <cmath>
#include <cstddef>
#include <vector>
#include "SizeGrid.h"
#include "StreamSlice.h"

namespace stosis
{
    // Lage einer Groesse auf dem Raster als Kommazahl: 0 = erste
    // Stuetzstelle, numClasses - 1 = letzte; beim logarithmischen Raster
    // gleichmaessig in ln d. Ausserhalb [sizeMin, sizeMax] kleiner 0 bzw.
    // groesser numClasses - 1; d <= 0 auf dem logarithmischen Raster -> -1.
    inline double GetGridPosition(const SSizeGrid& grid, double size)
    {
        const double step = grid.GetStep();
        if (!(step > 0.0))
            return 0.0;
        if (grid.IsLogarithmic())
            return size > 0.0 ? std::log(size / grid.sizeMin) / step : -1.0;
        return (size - grid.sizeMin) / step;
    }

    // Karten -> Groessenraster, damit Heatmap, Kurven, Statistik und das
    // Fenster "Auswertung" im Partikelmodus unveraendert weiterarbeiten
    // (Methoden-Dokument 13.20). Jede Karte gibt ihren Massenstrom je Stoff
    // an die beiden benachbarten Stuetzstellen ab, aufgeteilt nach ihrer Lage
    // dazwischen (linear in d bzw. in ln d): liegt sie zu 30 % des Wegs von
    // d_i nach d_i+1, gehen 70 % an d_i und 30 % an d_i+1. Geteilt durch das
    // Trapezgewicht w_i (SSizeGrid::GetWeight) ergibt das die
    // Massenstromdichte. Dadurch gilt exakt
    //   Summe_i w_i * q_i = Summe der Kartenmassenstroeme   (Massenerhaltung)
    // und der massengewichtete Mittelwert von d (linear) bzw. ln d
    // (logarithmisch) bleibt erhalten. Karten ausserhalb [d_min, d_max]
    // landen ganz auf der Randstuetzstelle: ihre Masse bleibt, sie erscheint
    // als Spitze am Rand.
    //
    // out wird zu einer Kontinuums-Scheibe mit numClasses x Stoffe der
    // Karten (Speicher wird wiederverwendet), Temperatur und Druck uebernommen.
    // weights = grid.GetWeights(), vom Aufrufer einmal berechnet.
    inline void ProjectCards(const CStreamSlice& cards, const SSizeGrid& grid,
        const std::vector<double>& weights, CStreamSlice& out)
    {
        const size_t numS = grid.numClasses;
        const size_t numC = cards.GetNumCompounds();
        if (out.HasCards() || out.GetNumSizeClasses() != numS || out.GetNumCompounds() != numC)
            out.Resize(numS, numC);
        else
            out.Clear();
        out.SetTemperature(cards.GetTemperature());
        out.SetPressure(cards.GetPressure());
        if (numS == 0 || weights.size() < numS)
            return;

        for (size_t k = 0; k < cards.GetNumCards(); ++k)
        {
            size_t i = 0;
            double upper = 0.0; // Anteil fuer die Stuetzstelle i + 1
            if (numS >= 2)
            {
                const double x = GetGridPosition(grid, cards.GetCardSize(k));
                const double last = static_cast<double>(numS - 1);
                if (x >= last)
                {
                    i = numS - 2;
                    upper = 1.0;
                }
                else if (x > 0.0) // auch: NaN bleibt bei i = 0
                {
                    i = static_cast<size_t>(x);
                    upper = x - static_cast<double>(i);
                }
            }
            for (size_t c = 0; c < numC; ++c)
            {
                const double massFlow = cards.GetCardMassFlow(k, c);
                if (massFlow == 0.0)
                    continue;
                out.SetValue(c, i, out.GetValue(c, i) + massFlow * (1.0 - upper) / weights[i]);
                if (upper > 0.0)
                    out.SetValue(c, i + 1, out.GetValue(c, i + 1) + massFlow * upper / weights[i + 1]);
            }
        }
    }
}
