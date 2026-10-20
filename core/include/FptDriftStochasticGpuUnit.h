#pragma once
#include <vector>
#include <algorithm>
#include "TransportUnit.h"
#include "FptDriftKernel.h"

namespace stosis
{
    // GPU-Variante von CFptDriftStochasticParticleUnit. Baut die
    // Partikelliste genau wie die CPU-Version auf, ruft dann einmalig
    // RunFptDriftKernel auf (simuliert ALLE Partikel gleichzeitig auf
    // der GPU), und binnt die zurueckgegebenen Einzelergebnisse
    // anschliessend in einer schnellen, sequenziellen CPU-Schleife ins
    // Zeit-/Groessenraster.
    class CFptDriftStochasticGpuUnit : public ITransportUnit
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verweilzeit: Drift/Diffusion FPT (stochastisch, GPU)";
        }

        std::string GetResultLabel() const override
        {
            return "Austritts-Wahrscheinlichkeitsdichte (1/Zeit)";
        }

        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"tubeLength", "Rohrlaenge L", 0.1, 1000.0, 100.0},
                {"driftVelocityRef", "Referenz-Driftgeschwindigkeit v_ref", -50.0, 50.0, 10.0},
                {"diffusionCoeff", "Diffusionskoeffizient D", 0.01, 500.0, 20.0},
                {"referenceSize", "Referenzgroesse d_ref", 0.1, 500.0, 60.0},
                {"sizeExponent", "Groessenexponent p (auf Geschwindigkeit)", -5.0, 5.0, 2.0},
                {"numParticles", "Anzahl Partikel pro Groessenklasse", 10.0, 2000000.0, 2000.0},
                {"stepsPerOutput", "Sub-Schritte pro Ausgabe-Zeitschritt", 1.0, 200.0, 10.0},
                {"seed", "Zufalls-Seed", 0.0, 100000.0, 42.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "tubeLength") m_tubeLength = value;
            else if (id == "driftVelocityRef") m_driftVelocityRef = value;
            else if (id == "diffusionCoeff") m_diffusionCoeff = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
            else if (id == "numParticles") m_numParticles = value;
            else if (id == "stepsPerOutput") m_stepsPerOutput = value;
            else if (id == "seed") m_seed = value;
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
        CStream* GetOutputStream() override { return &m_output; }
        const CSimulationResults& GetResults() const override { return m_results; }

        void Simulate() override
        {
            if (!m_input || m_numClasses < 1 || m_numTimeSteps < 2)
                return;

            const int particlesPerClass = std::max(1, static_cast<int>(std::round(m_numParticles)));
            const int stepsPerOutput = std::max(1, static_cast<int>(std::round(m_stepsPerOutput)));
            const double dx = (m_numClasses > 1)
                ? (m_sizeMax - m_sizeMin) / static_cast<double>(m_numClasses - 1)
                : 1.0;
            const double dtOutput = m_totalTime / static_cast<double>(m_numTimeSteps - 1);

            m_results.Configure(m_numTimeSteps, dtOutput, m_numClasses, m_sizeMin, m_sizeMax);

            // --- Partikel-Listen aufbauen, genau wie CParticleEnsemble ---
            const std::vector<double>& amplitude = m_input->GetValues();
            std::vector<double> particleSizes;
            std::vector<double> particleWeights;

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;
                if (amp <= 0.0)
                    continue;

                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double classSize = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double particleWeight = amp / static_cast<double>(particlesPerClass);

                for (int p = 0; p < particlesPerClass; ++p)
                {
                    particleSizes.push_back(classSize);
                    particleWeights.push_back(particleWeight);
                }
            }

            // --- Einmaliger Kernel-Aufruf: ALLE Partikel gleichzeitig ---
            std::vector<SParticleResultGpu> gpuResults;
            RunFptDriftKernel(particleSizes, particleWeights,
                m_driftVelocityRef, m_diffusionCoeff,
                m_referenceSize, m_sizeExponent,
                m_tubeLength, m_totalTime,
                stepsPerOutput, static_cast<int>(m_numTimeSteps),
                static_cast<unsigned int>(m_seed),
                gpuResults);

            // --- Sequenzielles Zusammenfuehren auf der CPU -----------------
            std::vector<double> accum(m_numTimeSteps * m_numClasses, 0.0);
            auto idx = [this](size_t t, size_t s) { return t * m_numClasses + s; };

            for (const SParticleResultGpu& result : gpuResults)
            {
                if (!result.exited)
                    continue;

                const int timeBin = static_cast<int>(result.exitTime / dtOutput);
                if (timeBin < 0 || timeBin >= static_cast<int>(m_numTimeSteps))
                    continue;

                int sizeBin = static_cast<int>(std::round((result.size - m_sizeMin) / dx));
                sizeBin = std::clamp(sizeBin, 0, static_cast<int>(m_numClasses) - 1);

                accum[idx(static_cast<size_t>(timeBin), static_cast<size_t>(sizeBin))] += result.weight;
            }

            for (size_t t = 0; t < m_numTimeSteps; ++t)
                for (size_t s = 0; s < m_numClasses; ++s)
                    m_results.SetValue(t, s, accum[idx(t, s)] / dtOutput);

            std::vector<double> finalValues(m_numClasses);
            for (size_t s = 0; s < m_numClasses; ++s)
                finalValues[s] = m_results.GetValue(m_numTimeSteps - 1, s);
            m_output.SetValues(finalValues);
        }

    private:
        size_t m_numClasses = 0;
        double m_sizeMin = 0.0;
        double m_sizeMax = 0.0;
        size_t m_numTimeSteps = 0;
        double m_totalTime = 0.0;

        double m_tubeLength = 100.0;
        double m_driftVelocityRef = 10.0;
        double m_diffusionCoeff = 20.0;
        double m_referenceSize = 60.0;
        double m_sizeExponent = 2.0;
        double m_numParticles = 2000.0;
        double m_stepsPerOutput = 10.0;
        double m_seed = 42.0;

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
    };
}