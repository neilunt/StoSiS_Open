#pragma once
#include <vector>
#include <random>
#include "Particle.h"
#include "Stream.h"

namespace stosis
{
    // Verwaltet eine Sammlung von Partikeln. Aktuell nur zustaendig
    // fuer die Initialisierung aus einem bin-basierten CStream heraus -
    // die Startverteilung selbst kommt aus der
    // Source-Einheit als Werte pro Groessenklasse, ab hier existiert
    // jedes Partikel als eigenstaendiges Objekt mit freier Groesse statt
    // als Bin-Index.
    class CParticleEnsemble
    {
    public:
        // Erzeugt particlesPerClass Partikel pro Groessenklasse, deren
        // Groesse auf das Zentrum der jeweiligen Klasse gesetzt wird
        // und deren Gewicht sich aus dem Amplitudenwert dieser Klasse
        // ergibt (amplitude / particlesPerClass), analog zu den
        // uebrigen stochastischen Einheiten. Klassen mit Amplitude
        // <= 0 werden uebersprungen, spart Rechenzeit.
        void InitializeFromStream(const CStream& input,
            double sizeMin, double sizeMax, size_t numSizeClasses,
            int particlesPerClass)
        {
            m_particles.clear();

            const std::vector<double>& amplitude = input.GetValues();

            for (size_t s = 0; s < numSizeClasses; ++s)
            {
                const double amp = (s < amplitude.size()) ? amplitude[s] : 0.0;
                if (amp <= 0.0)
                    continue;

                const double frac = static_cast<double>(s) / static_cast<double>(numSizeClasses - 1);
                const double classSize = sizeMin + frac * (sizeMax - sizeMin);
                const double particleWeight = amp / static_cast<double>(particlesPerClass);

                for (int p = 0; p < particlesPerClass; ++p)
                {
                    SParticle particle;
                    particle.position = 0.0;
                    particle.size = classSize;
                    particle.weight = particleWeight;
                    m_particles.push_back(particle);
                }
            }
        }

        std::vector<SParticle>& GetParticles() { return m_particles; }
        const std::vector<SParticle>& GetParticles() const { return m_particles; }
        size_t GetCount() const { return m_particles.size(); }

    private:
        std::vector<SParticle> m_particles;
    };
}