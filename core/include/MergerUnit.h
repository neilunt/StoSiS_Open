#pragma once
#include <set>
#include <string>
#include <vector>
#include "FlowsheetUnit.h"
#include "StreamMixing.h"

namespace stosis
{
    // Fuehrt N Eingangsstroeme (aktuell 2 oder 3) zu einem Ausgang
    // zusammen. Deterministische Variante: einfache Summe je Stoff und
    // Groessenklasse in jedem Schritt; Temperatur aus der Energiebilanz,
    // Druck = kleinster Eingangsdruck (MixConditions). Nicht verbundene
    // Eingaenge zaehlen als 0. Kein Hold-up. Ein Merger ist typischerweise
    // die Stelle, an der eine Rueckfuehrung wieder einmuendet. Im
    // Partikelmodus haengt er die Karten aller Eingaenge aneinander.
    class CMergerUnit : public IFlowsheetUnit
    {
    public:
        explicit CMergerUnit(size_t numInputs)
            : m_numInputs(numInputs)
        {
        }

        size_t GetNumInputs() const { return m_numInputs; }

        void Initialize(const SSimulationGrid& grid, std::uint64_t /*unitSeed*/) override
        {
            m_compounds = grid.compounds;
            m_materials = grid.materials;
            m_sizeWeights = grid.GetSizeGrid().GetWeights();
            m_missingHeatCapacity.clear();
        }

        void Step(const SStepContext& /*context*/, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            if (outputs.empty())
                return;
            CStreamSlice& out = outputs[0];
            m_parts.clear();
            for (const CStreamSlice* input : inputs)
            {
                if (!input)
                    continue;
                // Partikelmodus: Karten aneinanderhaengen (zusammengelegt wird
                // erst mit den Grenzen je Stromscheibe, Methoden-Dokument 13.19).
                if (out.HasCards())
                    out.AppendCards(*input);
                else
                    out.Add(*input);
                SMixPart part;
                part.temperature = input->GetTemperature();
                part.pressure = input->GetPressure();
                part.compoundWeights.resize(input->GetNumCompounds());
                for (size_t c = 0; c < part.compoundWeights.size(); ++c)
                    part.compoundWeights[c] = input->HasCards() ? input->GetCardsMassFlow(c)
                                                                : input->GetCompoundWeight(c, m_sizeWeights);
                m_parts.push_back(std::move(part));
            }
            if (m_parts.empty())
                return; // nichts verbunden: Raumbedingungen aus Clear()

            const SMixResult mix = MixConditions(m_parts, m_compounds, m_materials);
            out.SetTemperature(mix.temperature);
            out.SetPressure(mix.pressure);
            m_missingHeatCapacity.insert(mix.missingHeatCapacity.begin(), mix.missingHeatCapacity.end());
        }

        bool SupportsParticles() const override { return true; }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            for (const std::string& name : m_missingHeatCapacity)
                warnings.push_back("Keine Waermekapazitaet fuer " + name +
                    " - Mischtemperatur nur nach Masse gewichtet statt aus der Energiebilanz.");
            return warnings;
        }

    private:
        size_t m_numInputs = 0;
        std::vector<std::string> m_compounds;
        const CMaterialDatabase* m_materials = nullptr;
        std::vector<double> m_sizeWeights; // Trapezgewichte des Groessenrasters
        std::vector<SMixPart> m_parts; // wiederverwendet, spart Speicheranforderungen
        std::set<std::string> m_missingHeatCapacity;
    };
}
