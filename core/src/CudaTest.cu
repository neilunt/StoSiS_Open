#include "CudaTest.h"
#include <cuda_runtime.h>
#include <sstream>

namespace stosis
{
    bool GetCudaDeviceInfo(std::string& outInfo)
    {
        int deviceCount = 0;
        cudaError_t err = cudaGetDeviceCount(&deviceCount);

        if (err != cudaSuccess || deviceCount == 0)
        {
            outInfo = "Kein CUDA-faehiges Geraet gefunden.";
            return false;
        }

        std::ostringstream oss;
        oss << deviceCount << " CUDA-Geraet(e) gefunden:\n\n";

        for (int i = 0; i < deviceCount; ++i)
        {
            cudaDeviceProp prop{};
            cudaGetDeviceProperties(&prop, i);

            oss << "Geraet " << i << ": " << prop.name << "\n";
            oss << "  Compute Capability: " << prop.major << "." << prop.minor << "\n";
            oss << "  Speicher: " << (prop.totalGlobalMem / (1024 * 1024)) << " MB\n\n";
        }

        outInfo = oss.str();
        return true;
    }
}