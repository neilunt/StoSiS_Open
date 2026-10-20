#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "Materials.h"

namespace stosis
{
    // Ein Beitrag zu einer Mischung (ein Eingang eines Mergers, das Material
    // aus einem Zeitpunkt im Hold-up ...): Menge je Stoff bei Temperatur und
    // Druck. Die Einheit der Mengen ist beliebig, muss aber fuer alle
    // Beitraege gleich sein (z.B. CStreamSlice::GetCompoundWeight).
    struct SMixPart
    {
        std::vector<double> compoundWeights;
        double temperature = kRoomTemperature;
        double pressure = kRoomPressure;
    };

    struct SMixResult
    {
        double temperature = kRoomTemperature;
        double pressure = kRoomPressure;
        // Stoffe ohne Waermekapazitaet. Dann ist die Temperatur nur nach
        // Masse gewichtet statt aus der Energiebilanz berechnet.
        std::vector<std::string> missingHeatCapacity;
    };

    // Temperatur und Druck einer Mischung:
    //  - Druck: kleinster Druck der Beitraege mit Material. An einer
    //    Zusammenfuehrung herrscht nur ein Druck, und Material stroemt nur
    //    vom hoeheren zum niedrigeren (wie Dyssol).
    //  - Temperatur aus der Energiebilanz
    //        sum_c W_c h_c(T) = sum_i sum_c w_ic h_c(T_i),
    //    W_c = sum_i w_ic, h_c aus c_p (SMaterial::GetSpecificEnthalpy).
    //    Haben alle Beitraege dieselbe Temperatur, bleibt sie exakt erhalten.
    //  - Ohne Material: Mittel der Temperaturen und kleinster Druck aller
    //    Beitraege - dann sind beide nur Anzeigewerte.
    inline SMixResult MixConditions(const std::vector<SMixPart>& parts,
        const std::vector<std::string>& compoundNames, const CMaterialDatabase* materials)
    {
        SMixResult result;
        if (parts.empty())
            return result;

        auto totalWeight = [](const SMixPart& part)
        {
            double sum = 0.0;
            for (double w : part.compoundWeights)
                sum += w;
            return sum;
        };

        std::vector<const SMixPart*> filled;
        for (const SMixPart& part : parts)
            if (totalWeight(part) > 0.0)
                filled.push_back(&part);

        if (filled.empty())
        {
            double sumT = 0.0;
            result.pressure = parts.front().pressure;
            for (const SMixPart& part : parts)
            {
                sumT += part.temperature;
                result.pressure = std::min(result.pressure, part.pressure);
            }
            result.temperature = sumT / static_cast<double>(parts.size());
            return result;
        }

        result.pressure = filled.front()->pressure;
        double tMin = filled.front()->temperature;
        double tMax = tMin;
        for (const SMixPart* part : filled)
        {
            result.pressure = std::min(result.pressure, part->pressure);
            tMin = std::min(tMin, part->temperature);
            tMax = std::max(tMax, part->temperature);
        }
        if (tMin == tMax)
        {
            result.temperature = tMin;
            return result;
        }

        // Massengewichtete Temperatur: Startwert und Notloesung.
        double sumW = 0.0;
        double sumWT = 0.0;
        for (const SMixPart* part : filled)
        {
            const double w = totalWeight(*part);
            sumW += w;
            sumWT += w * part->temperature;
        }
        const double massWeighted = sumWT / sumW;

        // Menge je Stoff und die Stoffdaten dazu.
        size_t numC = 0;
        for (const SMixPart* part : filled)
            numC = std::max(numC, part->compoundWeights.size());
        std::vector<double> W(numC, 0.0);
        for (const SMixPart* part : filled)
            for (size_t c = 0; c < part->compoundWeights.size(); ++c)
                W[c] += part->compoundWeights[c];

        std::vector<const SMaterial*> material(numC, nullptr);
        for (size_t c = 0; c < numC; ++c)
        {
            if (W[c] <= 0.0)
                continue;
            const std::string name = c < compoundNames.size() ? compoundNames[c] : std::string();
            material[c] = (materials && !name.empty()) ? materials->FindByName(name) : nullptr;
            if (!material[c] || !material[c]->FindProperty("heat_capacity"))
                result.missingHeatCapacity.push_back(name.empty() ? std::string("unbekannter Stoff") : name);
        }
        if (!result.missingHeatCapacity.empty())
        {
            result.temperature = massWeighted;
            return result;
        }

        const double p = result.pressure;
        auto enthalpy = [&](size_t c, double T)
        {
            double h = 0.0;
            material[c]->GetSpecificEnthalpy(T, p, h);
            return h;
        };

        double target = 0.0;
        for (const SMixPart* part : filled)
            for (size_t c = 0; c < part->compoundWeights.size(); ++c)
                if (part->compoundWeights[c] > 0.0)
                    target += part->compoundWeights[c] * enthalpy(c, part->temperature);

        // Newton mit Bisektion als Sicherung. Bei positivem c_p liegt die
        // Mischtemperatur zwischen der kaeltesten und der waermsten.
        double lo = tMin;
        double hi = tMax;
        double T = massWeighted;
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            double f = -target;
            double slope = 0.0;
            for (size_t c = 0; c < numC; ++c)
            {
                if (W[c] <= 0.0)
                    continue;
                f += W[c] * enthalpy(c, T);
                slope += W[c] * material[c]->GetProperty("heat_capacity", T, p).value;
            }
            if (f == 0.0)
                break;
            if (f > 0.0)
                hi = T;
            else
                lo = T;
            double next = slope > 0.0 ? T - f / slope : 0.5 * (lo + hi);
            if (std::abs(next - T) <= 1e-12 * T)
            {
                T = next;
                break;
            }
            if (!(next > lo && next < hi))
                next = 0.5 * (lo + hi);
            T = next;
        }
        result.temperature = T;
        return result;
    }
}
