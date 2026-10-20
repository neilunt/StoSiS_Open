# stosis_core: reine C++ Bibliothek, KEINE Qt-Abhaengigkeit.
# Enthaelt Flowsheet-Logik, Unit-Interface, spaeter Partikelgruppen +
# CUDA-Kernel. Muss aus einem Kommandozeilen-Tool oder Unit-Test heraus
# nutzbar sein, ganz ohne GUI.

add_library(stosis_core STATIC
    src/Flowsheet.cpp
    src/Unit.cpp
    src/BuiltinTransportUnits.cpp
    src/MaterialDatabase.cpp
    include/Materials.h
    include/StreamMixing.h
    include/StreamRecord.h
    include/Flowsheet.h
    include/Unit.h
    include/Stream.h
    include/SourceUnit.h
    include/ScalerUnit.h
    include/SinkUnit.h
    include/SimulationResults.h
    include/DistributionSourceUnit.h
    include/DiffusionDriftUnit.h
    include/TransportUnit.h
    include/TransportUnitRegistry.h
    include/BuiltinTransportUnits.h
    include/TisDeterministicUnit.h
    include/TisStochasticUnit.h
    include/SimulationResultsDiff.h
    include/WienerDriftStochasticUnit.h
    include/FptDriftDeterministicUnit.h
    include/FptDriftStochasticUnit.h
include/Particle.h
include/ParticleEnsemble.h
include/FptDriftStochasticParticleUnit.h
include/FptDriftStochasticGpuUnit.h
include/InletUnit.h
include/OutletUnit.h
include/FlowsheetGraph.h
include/StreamProcessor.h
include/StreamProcessorRegistry.h
include/ParameterDescriptor.h
include/SizeDependentDelayProcessor.h
include/BuiltinStreamProcessors.h
include/SizeDistribution.h
include/SplitterUnit.h
include/MergerUnit.h
include/StreamSlice.h
include/FlowsheetUnit.h
include/Random.h
include/EnsembleStatistics.h
)

target_include_directories(stosis_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# JSON-Bibliothek (nlohmann/json 3.12.0, MIT-Lizenz, nur Header) zum Lesen
# der Materialdatei. PRIVATE: nur MaterialDatabase.cpp braucht sie.
target_include_directories(stosis_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
)

set_target_properties(stosis_core PROPERTIES FOLDER "Core")

# --- CUDA-Anbindung -----------------------------------------------------
add_library(stosis_cuda STATIC
    src/CudaTest.cu
    include/CudaTest.h
src/FptDriftKernel.cu
include/FptDriftKernel.h
include/ParticleResultGpu.h
)

set_target_properties(stosis_cuda PROPERTIES
    CUDA_ARCHITECTURES "52;89"
    FOLDER "Core"
)

target_include_directories(stosis_cuda PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(stosis_core PUBLIC stosis_cuda)
