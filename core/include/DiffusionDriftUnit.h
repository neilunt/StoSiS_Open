#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <utility>
#include "TransportUnit.h"
#include "Stream.h"
#include "SimulationResults.h"

namespace stosis
{
    // Erste "echte" Rechen-Unit (statt Formel-Demo): loest deterministisch
    // die Advektions-Diffusionsgleichung
    //     dc/dt = -v * dc/dx + D * d2c/dx2
    // explizit ueber die Zeit, mit Upwind fuer den Drift-Term (stabil
    // fuer beide Vorzeichen von v) und zentralen Differenzen fuer die
    // Diffusion. Raender: c=0 (absorbierend).
    //
    // Genau wie bei deinen echten Dyssol-Units: Simulate() wird vom
    // Flowsheet nur EINMAL aufgerufen (lineares Flowsheet ohne
    // Rueckfuehrung), die Unit iteriert deshalb selbst ueber ihr
    // eigenes feines Zeitgitter und schreibt bei jedem Sub-Schritt in
    // die Ergebnis-Matrix - genau das Muster, das du schon von deiner
    // Gate-Unit kennst.
    class CDiffusionDriftUnit : public ITransportUnit
    {
    public:

        std::string GetTypeName() const override
        {
            return "Partikelwachstum: Diffusion/Drift (deterministisch)";
        }

        std::string GetResultLabel() const override
        {
            return "Partikeldichte (relativ)";
        }


        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"diffusionCoeff", "Diffusionskoeffizient D", 0.0, 1000.0, 40.0},
                {"driftVelocity", "Driftgeschwindigkeit v", -100.0, 100.0, 0.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "diffusionCoeff")
                m_diffusionCoeff = value;
            else if (id == "driftVelocity")
                m_driftVelocity = value;
        }


        void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
                        size_t numTimeSteps, double totalTime) override
        {
            m_numClasses = numClasses;
            m_sizeMin = sizeMin;
            m_sizeMax = sizeMax;
            m_numTimeSteps = numTimeSteps;
            m_totalTime = totalTime;
        }

        void SetInputStream(CStream* input) override { m_input = input; }
        CStream* GetOutputStream() override {return &m_output; }
        const CSimulationResults& GetResults() const override { return m_results; }

        void Simulate() override
        {
            if (!m_input || m_numClasses < 3 || m_numTimeSteps < 2)
                return;

            const double dx = (m_sizeMax - m_sizeMin) / static_cast<double>(m_numClasses - 1);
            const double dtOutput = m_totalTime / static_cast<double>(m_numTimeSteps - 1);

            m_results.Configure(m_numTimeSteps, dtOutput, m_numClasses, m_sizeMin, m_sizeMax);

            // Startzustand (t=0) direkt vom Input-Stream uebernehmen.
            std::vector<double> current = m_input->GetValues();
            current.resize(m_numClasses, 0.0);
            for (size_t s = 0; s < m_numClasses; ++s)
                m_results.SetValue(0, s, current[s]);

            // --- Stabilitaet: interne Sub-Schritte bestimmen -----------
            // Explizite Diffusion braucht microDt <= dx^2 / (2*D).
            // Upwind-Advektion braucht microDt <= dx / |v| (CFL-Bedingung).
            // Sicherheitsfaktor 0.4 statt der theoretischen Grenze, damit
            // numerisches Rauschen nicht die Stabilitaet auffrisst.
            double microDtMax = dtOutput;
            if (m_diffusionCoeff > 0.0)
                microDtMax = std::min(microDtMax, 0.4 * dx * dx / m_diffusionCoeff);
            if (std::abs(m_driftVelocity) > 1e-12)
                microDtMax = std::min(microDtMax, 0.4 * dx / std::abs(m_driftVelocity));

            const int numSubSteps = std::max(1, static_cast<int>(std::ceil(dtOutput / microDtMax)));
            const double microDt = dtOutput / numSubSteps;

            std::vector<double> next(m_numClasses, 0.0);

            for (size_t outStep = 1; outStep < m_numTimeSteps; ++outStep)
            {
                for (int sub = 0; sub < numSubSteps; ++sub)
                {
                    for (size_t i = 1; i + 1 < m_numClasses; ++i)
                    {
                        const double diffusionTerm =
                            m_diffusionCoeff * (current[i + 1] - 2.0 * current[i] + current[i - 1]) / (dx * dx);

                        // Upwind: bei positiver Drift-Geschwindigkeit
                        // "kommt die Information von links", bei negativer
                        // von rechts - so bleibt das Schema stabil.
                        double advectionTerm;
                        if (m_driftVelocity >= 0.0)
                            advectionTerm = m_driftVelocity * (current[i] - current[i - 1]) / dx;
                        else
                            advectionTerm = m_driftVelocity * (current[i + 1] - current[i]) / dx;

                        next[i] = current[i] + microDt * (diffusionTerm - advectionTerm);
                    }
                    next[0] = 0.0;
                    next[m_numClasses - 1] = 0.0;
                    std::swap(current, next);
                }
                for (size_t s = 0; s < m_numClasses; ++s)
                    m_results.SetValue(outStep, s, current[s]);
            }

            m_output.SetValues(current);
        }

    private:
        size_t m_numClasses = 0;
        double m_sizeMin = 0.0;
        double m_sizeMax = 0.0;
        size_t m_numTimeSteps = 0;
        double m_totalTime = 0.0;
        double m_diffusionCoeff = 40.0;
        double m_driftVelocity = 0.0;

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}
