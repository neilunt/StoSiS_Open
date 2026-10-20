#pragma once
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include "StreamProcessor.h"
#include "StreamMixing.h"

namespace stosis
{
    // Erste konkrete Verarbeitungseinheit: verzoegert jede Groessenklasse
    // um eine groessenabhaengige Zeit
    //   delay(size) = delayRef * (size / referenceSize)^sizeExponent
    // Kein Zufall, keine Impulsantwort - der Eingangsverlauf kommt 1:1,
    // nur zeitlich verschoben, am Ausgang an.
    //
    // Hold-up: ein Ringpuffer mit den Eingaengen der letzten Schritte -
    // das ist das Material, das gerade "unterwegs" in der Einheit ist.
    // Er ist genau so lang, wie die groesste Verzoegerung es verlangt.
    // Jeder Platz merkt sich auch Temperatur und Druck seines Materials:
    // T und p wandern mit dem Material. Kommt Material aus mehreren
    // Zeitpunkten gleichzeitig an (verschiedene Groessen sind verschieden
    // lang unterwegs), mischt MixConditions es wie ein Merger.
    class CSizeDependentDelayProcessor : public IStreamProcessor
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verzoegerung: Groessenabhaengig";
        }

        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"delayRef", "Verzoegerung bei Referenzgroesse [s]", 0.0, 100.0, 2.0},
                {"referenceSize", std::string("Referenzgroesse d_ref [") + kSizeDisplayUnit + "]", 1e-9, 1.0, 100e-6, kSizeDisplayFactor},
                {"sizeExponent", "Groessenexponent p", -5.0, 5.0, 1.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "delayRef") m_delayRef = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
        }

        double GetParameterValue(const std::string& id) const override
        {
            if (id == "delayRef") return m_delayRef;
            if (id == "referenceSize") return m_referenceSize;
            if (id == "sizeExponent") return m_sizeExponent;
            return 0.0;
        }

        void Initialize(const SSimulationGrid& grid, std::uint64_t /*unitSeed*/) override
        {
            m_dt = grid.GetDt();
            const size_t numS = grid.numSizeClasses;
            m_numCompounds = grid.GetNumCompounds();
            m_compounds = grid.compounds;
            m_materials = grid.materials;
            m_missingHeatCapacity.clear();

            m_sizeWeights = grid.GetSizeGrid().GetWeights();
            m_delays.resize(numS);
            double maxDelay = 0.0;
            for (size_t s = 0; s < numS; ++s)
            {
                const double safeSize = std::max(grid.GetSizeAt(s), 1e-12);
                m_delays[s] = m_delayRef * std::pow(safeSize / m_referenceSize, m_sizeExponent);
                maxDelay = std::max(maxDelay, m_delays[s]);
            }

            // Puffer muss die Eingaenge von "jetzt" bis "jetzt - maxDelay"
            // (plus einen Nachbarpunkt zum Interpolieren) halten. Laenger als
            // die ganze Simulation muss er nie sein - noch aeltere Werte gibt
            // es nicht, dort ist der Ausgang 0.
            size_t capacity = grid.numTimeSteps + 1;
            if (m_dt > 0.0 && maxDelay / m_dt < static_cast<double>(capacity))
                capacity = std::min(capacity, static_cast<size_t>(std::ceil(maxDelay / m_dt)) + 2);
            m_history.assign(capacity, std::vector<double>(m_numCompounds * numS, 0.0));
            m_historyTemperature.assign(capacity, kRoomTemperature);
            m_historyPressure.assign(capacity, kRoomPressure);
            m_slotWeights.assign(capacity * m_numCompounds, 0.0);
        }

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            if (outputs.empty() || m_history.empty())
                return;
            const size_t k = context.stepIndex;
            const size_t capacity = m_history.size();
            const size_t numS = m_delays.size();

            // Aktuellen Eingang in den Puffer legen (unverbunden = 0).
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            const size_t numC = m_numCompounds;
            const size_t current = k % capacity;
            std::vector<double>& slot = m_history[current];
            for (size_t c = 0; c < numC; ++c)
                for (size_t s = 0; s < numS; ++s)
                    slot[c * numS + s] = (input && c < input->GetNumCompounds() && s < input->GetNumSizeClasses())
                        ? input->GetValue(c, s) : 0.0;
            m_historyTemperature[current] = input ? input->GetTemperature() : kRoomTemperature;
            m_historyPressure[current] = input ? input->GetPressure() : kRoomPressure;

            // Wie viel Material jedes Pufferplatzes jetzt austritt (je Stoff,
            // Trapezgewichte des Rasters wie CStreamSlice::GetCompoundWeight) - fuer T und p.
            std::fill(m_slotWeights.begin(), m_slotWeights.end(), 0.0);

            CStreamSlice& out = outputs[0];
            const size_t numOutS = std::min(numS, out.GetNumSizeClasses());
            for (size_t s = 0; s < numOutS; ++s)
            {
                // Zeitpunkt im Eingangssignal, der jetzt am Ausgang ankommt.
                const double sourceTime = static_cast<double>(k) * m_dt - m_delays[s];
                if (sourceTime < 0.0 || m_dt <= 0.0)
                    continue; // noch nichts angekommen -> bleibt 0

                const double trapezoid = m_sizeWeights[s];
                const double sourceIndex = sourceTime / m_dt;
                const size_t i0 = static_cast<size_t>(sourceIndex);
                if (i0 >= k)
                {
                    for (size_t c = 0; c < numC; ++c)
                    {
                        const double value = m_history[current][c * numS + s];
                        out.SetValue(c, s, value);
                        m_slotWeights[current * numC + c] += trapezoid * value;
                    }
                    continue;
                }
                if (k - i0 >= capacity)
                    continue; // aelter als der Puffer (sollte nicht vorkommen)

                const double w = sourceIndex - static_cast<double>(i0);
                const size_t a = i0 % capacity;
                const size_t b = (i0 + 1) % capacity;
                for (size_t c = 0; c < numC; ++c)
                {
                    const double valueA = m_history[a][c * numS + s];
                    const double valueB = m_history[b][c * numS + s];
                    out.SetValue(c, s, (1.0 - w) * valueA + w * valueB);
                    m_slotWeights[a * numC + c] += trapezoid * (1.0 - w) * valueA;
                    m_slotWeights[b * numC + c] += trapezoid * w * valueB;
                }
            }

            // T und p am Ausgang: Mischung der Pufferplaetze, aus denen gerade
            // Material kommt. Kommt nichts an, gelten die Werte des Eingangs.
            m_parts.clear();
            for (size_t i = 0; i < capacity; ++i)
            {
                const auto first = m_slotWeights.begin() + static_cast<std::ptrdiff_t>(i * numC);
                if (std::none_of(first, first + static_cast<std::ptrdiff_t>(numC), [](double v) { return v > 0.0; }))
                    continue;
                SMixPart part;
                part.compoundWeights.assign(first, first + static_cast<std::ptrdiff_t>(numC));
                part.temperature = m_historyTemperature[i];
                part.pressure = m_historyPressure[i];
                m_parts.push_back(std::move(part));
            }
            if (m_parts.empty())
            {
                out.SetTemperature(input ? input->GetTemperature() : kRoomTemperature);
                out.SetPressure(input ? input->GetPressure() : kRoomPressure);
                return;
            }
            const SMixResult mix = MixConditions(m_parts, m_compounds, m_materials);
            out.SetTemperature(mix.temperature);
            out.SetPressure(mix.pressure);
            m_missingHeatCapacity.insert(mix.missingHeatCapacity.begin(), mix.missingHeatCapacity.end());
        }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            for (const std::string& name : m_missingHeatCapacity)
                warnings.push_back("Keine Waermekapazitaet fuer " + name +
                    " - Austrittstemperatur nur nach Masse gewichtet statt aus der Energiebilanz.");
            return warnings;
        }

    private:
        double m_delayRef = 2.0;
        double m_referenceSize = 100e-6; // m
        double m_sizeExponent = 1.0;

        double m_dt = 0.0;
        size_t m_numCompounds = 1;
        std::vector<std::string> m_compounds;
        const CMaterialDatabase* m_materials = nullptr;
        std::vector<double> m_delays;               // pro Groessenklasse
        std::vector<double> m_sizeWeights;          // Trapezgewichte des Groessenrasters
        std::vector<std::vector<double>> m_history; // Ringpuffer [Schritt % Kapazitaet][Stoff * numS + Klasse]
        std::vector<double> m_historyTemperature;   // je Pufferplatz [K]
        std::vector<double> m_historyPressure;      // je Pufferplatz [Pa]
        std::vector<double> m_slotWeights;          // [Pufferplatz * numC + Stoff], je Schritt neu
        std::vector<SMixPart> m_parts;
        std::set<std::string> m_missingHeatCapacity;
    };
}
