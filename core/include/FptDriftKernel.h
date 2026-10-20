#pragma once
#include <vector>
#include "ParticleResultGpu.h"

namespace stosis
{
    // Schmale, reine C++ Schnittstelle zum GPU-Kernel. Nimmt die
    // Startgroessen und Gewichte einer Partikelmenge entgegen (Position
    // startet immer bei 0), simuliert jedes Partikel auf einem eigenen
    // GPU-Thread bis zum Austritt oder Simulationsende, und gibt fuer
    // jedes Partikel Austrittszeit, Groesse, Gewicht zurueck. Reines
    // Binning ins Zeit-/Groessenraster passiert bewusst NICHT hier,
    // sondern hinterher auf der CPU, wie besprochen.
    void RunFptDriftKernel(const std::vector<double>& particleSizes,
        const std::vector<double>& particleWeights,
        double driftVelocityRef, double diffusionCoeff,
        double referenceSize, double sizeExponent,
        double tubeLength, double totalTime,
        int stepsPerOutput, int numTimeSteps,
        unsigned int seed,
        std::vector<SParticleResultGpu>& outResults);
}