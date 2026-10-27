#pragma once
#include "FlowsheetUnit.h"
#include "SimulationResults.h"
#include "ResultsSource.h"
#include "StreamRecord.h"

namespace stosis
{
    // Senke eines Flowsheets: ein Eingang, kein Ausgang. Zeichnet in jedem
    // Makroschritt den ankommenden Strom auf (Gesamtmenge, je Stoff,
    // Temperatur, Druck) - das zeigen dann Kurven und Heatmap an.
    class COutletUnit : public IFlowsheetUnit, public IResultsSource
    {
    public:
        void Initialize(const SSimulationGrid& grid, std::uint64_t /*unitSeed*/) override
        {
            m_record.Configure(grid);
        }

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& /*outputs*/) override
        {
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            if (!input)
                return; // Zeile bleibt 0 (aus Configure)
            m_record.Record(context.stepIndex, *input);
        }

        // Karten rechnet CStreamRecord fuer die Anzeige aufs Raster um.
        bool SupportsParticles() const override { return true; }

        const CSimulationResults& GetResults() const override { return m_record.GetTotal(); }
        const CStreamRecord& GetRecord() const { return m_record; }

    private:
        CStreamRecord m_record;
    };
}
