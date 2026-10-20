#include "FptDriftKernel.h"
#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <algorithm>
#include <cmath>

namespace stosis
{
    // Laeuft auf der GPU, ein Thread pro Partikel. Adressierung ueber
    // die globale Thread-ID, kein Thread liest oder schreibt jemals
    // Speicher eines anderen Threads - deshalb keine atomaren
    // Operationen noetig.
    __global__ void FptDriftKernelFunc(const double* sizes, const double* weights,
        int numParticles,
        double driftVelocityRef, double diffusionCoeff,
        double referenceSize, double sizeExponent,
        double tubeLength, double microDt,
        int totalMicroSteps, unsigned int seed,
        double* outExitTime, bool* outExited)
    {
        const int i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= numParticles)
            return;

        // Eigener Zufallszahlen-Zustand pro Thread, initialisiert mit
        // Seed + Thread-Index, damit jeder Thread eine unabhaengige,
        // aber reproduzierbare Zufallsfolge bekommt.
        curandState rngState;
        curand_init(seed, static_cast<unsigned int>(i), 0, &rngState);

        const double safeSize = fmax(sizes[i], 1e-6);
        const double v_c = driftVelocityRef * pow(safeSize / referenceSize, -sizeExponent);
        const double sigmaSub = sqrt(2.0 * diffusionCoeff * microDt);

        double position = 0.0;
        bool exited = false;
        double exitTime = 0.0;

        for (int step = 1; step <= totalMicroSteps; ++step)
        {
            position += v_c * microDt + sigmaSub * curand_normal_double(&rngState);

            if (position >= tubeLength)
            {
                exited = true;
                exitTime = static_cast<double>(step) * microDt;
                break;
            }
        }

        outExitTime[i] = exitTime;
        outExited[i] = exited;
    }

    void RunFptDriftKernel(const std::vector<double>& particleSizes,
        const std::vector<double>& particleWeights,
        double driftVelocityRef, double diffusionCoeff,
        double referenceSize, double sizeExponent,
        double tubeLength, double totalTime,
        int stepsPerOutput, int numTimeSteps,
        unsigned int seed,
        std::vector<SParticleResultGpu>& outResults)
    {
        const int numParticles = static_cast<int>(particleSizes.size());
        outResults.assign(numParticles, SParticleResultGpu{});
        if (numParticles == 0)
            return;

        const double dtOutput = totalTime / static_cast<double>(numTimeSteps - 1);
        const double microDt = dtOutput / stepsPerOutput;
        const int totalMicroSteps = numTimeSteps * stepsPerOutput;

        // --- Speicher auf der GPU anlegen und Eingaben hinueberkopieren ---
        double* d_sizes = nullptr;
        double* d_exitTime = nullptr;
        bool* d_exited = nullptr;

        cudaMalloc(&d_sizes, numParticles * sizeof(double));
        cudaMalloc(&d_exitTime, numParticles * sizeof(double));
        cudaMalloc(&d_exited, numParticles * sizeof(bool));

        cudaMemcpy(d_sizes, particleSizes.data(), numParticles * sizeof(double), cudaMemcpyHostToDevice);

        // --- Kernel starten: ein Thread pro Partikel ------------------
        const int threadsPerBlock = 256;
        const int numBlocks = (numParticles + threadsPerBlock - 1) / threadsPerBlock;

        FptDriftKernelFunc << <numBlocks, threadsPerBlock >> > (
            d_sizes, nullptr, numParticles,
            driftVelocityRef, diffusionCoeff, referenceSize, sizeExponent,
            tubeLength, microDt, totalMicroSteps, seed,
            d_exitTime, d_exited);

        cudaDeviceSynchronize();

        // --- Ergebnisse zurueckkopieren --------------------------------
        std::vector<double> exitTimes(numParticles);
        std::vector<char> exitedFlags(numParticles); // bool-Vektor umgeht std::vector<bool>-Sonderfall

        cudaMemcpy(exitTimes.data(), d_exitTime, numParticles * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(exitedFlags.data(), d_exited, numParticles * sizeof(bool), cudaMemcpyDeviceToHost);

        cudaFree(d_sizes);
        cudaFree(d_exitTime);
        cudaFree(d_exited);

        for (int i = 0; i < numParticles; ++i)
        {
            outResults[i].exitTime = exitTimes[i];
            outResults[i].size = particleSizes[i];
            outResults[i].weight = particleWeights[i];
            outResults[i].exited = (exitedFlags[i] != 0);
        }
    }
}