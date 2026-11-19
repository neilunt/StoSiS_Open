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

    // Neighbouring nodes of a card size: node i gets the share 1 - upper,
    // node i + 1 the share upper (linear in d or in ln d). Sizes outside
    // [sizeMin, sizeMax] go entirely to the boundary node.
    inline void GetGridNeighbours(const SSizeGrid& grid, double size, size_t& i, double& upper)
    {
        const size_t numS = grid.numClasses;
        i = 0;
        upper = 0.0;
        if (numS < 2)
            return;
        const double x = GetGridPosition(grid, size);
        const double last = static_cast<double>(numS - 1);
        if (x >= last)
        {
            i = numS - 2;
            upper = 1.0;
        }
        else if (x > 0.0) // NaN stays at i = 0 as well
        {
            i = static_cast<size_t>(x);
            upper = x - static_cast<double>(i);
        }
    }

    // Adds the amount of one card (compound c, a mass flow or a mass) to the
    // nodes i and i + 1 as a density: divided by the trapezoid weights.
    inline void AddAtGridNodes(CStreamSlice& out, size_t c, size_t i, double upper, double amount,
        const std::vector<double>& weights)
    {
        if (amount == 0.0)
            return;
        out.SetValue(c, i, out.GetValue(c, i) + amount * (1.0 - upper) / weights[i]);
        if (upper > 0.0)
            out.SetValue(c, i + 1, out.GetValue(c, i + 1) + amount * upper / weights[i + 1]);
    }

    // Karten -> Groessenraster, damit Heatmap, Kurven, Statistik und das
    // Fenster "Auswertung" im Partikelmodus unveraendert weiterarbeiten
    // (Methodenhandbuch 9.6). Jede Karte gibt ihren Massenstrom je Stoff
    // (Masse im Paket / dt)
    // an die beiden benachbarten Stuetzstellen ab, aufgeteilt nach ihrer Lage
    // dazwischen (linear in d bzw. in ln d): liegt sie zu 30 % des Wegs von
    // d_i nach d_i+1, gehen 70 % an d_i und 30 % an d_i+1. Geteilt durch das
    // Trapezgewicht w_i (SSizeGrid::GetWeight) ergibt das die
    // Massenstromdichte. Dadurch gilt exakt
    //   Summe_i w_i * q_i = Summe der Kartenmassen / dt   (Massenerhaltung)
    // und der massengewichtete Mittelwert von d (linear) bzw. ln d
    // (logarithmisch) bleibt erhalten. Karten ausserhalb [d_min, d_max]
    // landen ganz auf der Randstuetzstelle: ihre Masse bleibt, sie erscheint
    // als Spitze am Rand.
    //
    // out wird zu einer Kontinuums-Scheibe mit numClasses x Stoffe der
    // Karten (Speicher wird wiederverwendet), Temperatur und Druck uebernommen.
    // weights = grid.GetWeights(), vom Aufrufer einmal berechnet; dt = Dauer
    // eines Zeitschritts [s] (Karten tragen Massen, das Raster Massenstroeme).
    inline void ProjectCards(const CStreamSlice& cards, const SSizeGrid& grid,
        const std::vector<double>& weights, double dt, CStreamSlice& out)
    {
        const size_t numS = grid.numClasses;
        const size_t numC = cards.GetNumCompounds();
        if (out.HasCards() || out.GetNumSizeClasses() != numS || out.GetNumCompounds() != numC)
            out.Resize(numS, numC);
        else
            out.Clear();
        out.SetTemperature(cards.GetTemperature());
        out.SetPressure(cards.GetPressure());
        if (numS == 0 || weights.size() < numS || !(dt > 0.0))
            return;
        const double perSecond = 1.0 / dt;

        for (size_t k = 0; k < cards.GetNumCards(); ++k)
        {
            size_t i = 0;
            double upper = 0.0;
            GetGridNeighbours(grid, cards.GetCardSize(k), i, upper);
            for (size_t c = 0; c < numC; ++c)
                AddAtGridNodes(out, c, i, upper, cards.GetCardMass(k, c) * perSecond, weights);
        }
    }
}
