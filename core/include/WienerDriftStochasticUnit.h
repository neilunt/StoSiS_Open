#pragma once
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include "TransportUnit.h"

namespace stosis
{
	//	Stochastisches Wiener Drift Modell:
	//	Jedes simulierte Partikel bewegt sich als Random Walk direkt im Groessenraum, angetrieben
	//	von Drift und Diffusion (Euler-Maruyama-Diskretisierung der SDE dX = v*dt + sqrt(2D)*dW)
	//	Bei genuegend Partikeln naehert sich das Ergebnis der deterministischen Loesung aus 
	//	CDiffusionDriftUnit an - gut geeignet fuer einen direkten Vergleich ueber die Vergleichsansicht
	class CWienerDriftStochasticUnit : public ITransportUnit
	{
	public:
		std::string GetTypeName() const override
		{
			return "Partikelwachstum: WienerDrift (stochastisch)";
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
				{"numParticles", "Anzahl Partikel pro Startklasse", 10.0, 200000.0, 500.0},
				{"stepsPerOutput", "Sub-Schritte pro Ausgabe-Zeitschritt", 1.0, 200.0, 10.0},
				{"seed", "Zufalls-Seed", 0.0, 100000.0, 42.0},
			};
		}

		void SetParameter(const std::string& id, double value) override
		{
			if (id == "diffusionCoeff") m_diffusionCoeff = value;
			else if (id == "driftVelocity") m_driftVelocity = value;
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
            if (!m_input || m_numClasses < 2 || m_numTimeSteps < 2)
                return;

            const double dx = (m_sizeMax - m_sizeMin) / static_cast<double>(m_numClasses - 1);
            const double dtOutput = m_totalTime / static_cast<double>(m_numTimeSteps - 1);
            const int stepsPerOutput = std::max(1, static_cast<int>(std::round(m_stepsPerOutput)));
            const double microDt = dtOutput / stepsPerOutput;
            const int numParticlesPerClass = std::max(1, static_cast<int>(std::round(m_numParticles)));

            m_results.Configure(m_numTimeSteps, dtOutput, m_numClasses, m_sizeMin, m_sizeMax);

            const std::vector<double>& amplitude = m_input->GetValues();

            // Lokaler Akkumulator statt direkt in m_results zu schreiben,
            // weil hier viele Partikel und Startklassen jeweils in
            // dieselbe Zelle einzahlen koennen (aufsummieren statt
            // ueberschreiben).
            std::vector<double> accum(m_numTimeSteps * m_numClasses, 0.0);
            auto idx = [this](size_t t, size_t s) { return t * m_numClasses + s; };

            // t=0: Startverteilung direkt uebernehmen, wie bei der
            // deterministischen Einheit auch.
            for (size_t s = 0; s < m_numClasses; ++s)
                accum[idx(0, s)] = (s < amplitude.size()) ? amplitude[s] : 0.0;

            std::mt19937 rng(static_cast<unsigned int>(m_seed));
            std::normal_distribution<double> normalDist(0.0, 1.0);
            const double sigmaStep = std::sqrt(2.0 * m_diffusionCoeff * microDt);

            for (size_t s0 = 0; s0 < m_numClasses; ++s0)
            {
                const double amp = (s0 < amplitude.size()) ? amplitude[s0] : 0.0;
                if (amp <= 0.0)
                    continue;

                const double frac0 = static_cast<double>(s0) / static_cast<double>(m_numClasses - 1);
                const double startSize = m_sizeMin + frac0 * (m_sizeMax - m_sizeMin);
                const double weight = amp / static_cast<double>(numParticlesPerClass);

                for (int p = 0; p < numParticlesPerClass; ++p)
                {
                    double position = startSize;
                    bool alive = true;

                    for (size_t outStep = 1; outStep < m_numTimeSteps; ++outStep)
                    {
                        if (alive)
                        {
                            for (int sub = 0; sub < stepsPerOutput; ++sub)
                            {
                                position += m_driftVelocity * microDt + sigmaStep * normalDist(rng);
                                if (position < m_sizeMin || position > m_sizeMax)
                                {
                                    alive = false;
                                    break;
                                }
                            }
                        }

                        if (alive)
                        {
                            int bin = static_cast<int>(std::round((position - m_sizeMin) / dx));
                            bin = std::clamp(bin, 0, static_cast<int>(m_numClasses) - 1);
                            accum[idx(outStep, static_cast<size_t>(bin))] += weight;
                        }
                        // ausgetretene Partikel tragen ab hier nichts mehr bei
                    }
                }
            }

            for (size_t t = 0; t < m_numTimeSteps; ++t)
                for (size_t s = 0; s < m_numClasses; ++s)
                    m_results.SetValue(t, s, accum[idx(t, s)]);

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

        double m_diffusionCoeff = 40.0;
        double m_driftVelocity = 0.0;
        double m_numParticles = 500.0;
        double m_stepsPerOutput = 10.0;
        double m_seed = 42.0;

        CStream* m_input = nullptr;
        CStream m_output;
        CSimulationResults m_results;
	};
}