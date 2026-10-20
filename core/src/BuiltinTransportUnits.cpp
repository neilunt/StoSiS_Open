#include "BuiltinTransportUnits.h"
#include "DiffusionDriftUnit.h"
#include "TisDeterministicUnit.h"
#include "TisStochasticUnit.h"
#include "WienerDriftStochasticUnit.h"
#include "FptDriftDeterministicUnit.h"
#include "FptDriftStochasticUnit.h"
#include "FptDriftStochasticParticleUnit.h"
#include "FptDriftStochasticGpuUnit.h"

namespace stosis {
	void RegisterBuiltinTransportUnits(CTransportUnitRegistry& registry) {

		registry.RegisterUnit("Partikelwachstum: Diffusion/Drift (deterministisch)", []() {
			return std::make_unique<CDiffusionDriftUnit>();
			});

		registry.RegisterUnit("Partikelwachstum: WienerDrift (stochastisch)", []() {
			return std::make_unique<CWienerDriftStochasticUnit>();
			});

		registry.RegisterUnit("Verweilzeit: TIS (deterministisch)", []() {
			return std::make_unique<CTisDeterministicUnit>();
			});

		registry.RegisterUnit("Verweilzeit: TIS (stochastisch, mit Rueckvermischung)", []() {
			return std::make_unique<CTisStochasticUnit>();
			});

		registry.RegisterUnit("Verweilzeit: Drift/Diffusion FPT (deterministisch)", []()
			{
				return std::make_unique<CFptDriftDeterministicUnit>();
			});

		registry.RegisterUnit("Verweilzeit: Drift/Diffusion FPT (stochastisch)", []()
			{
				return std::make_unique<CFptDriftStochasticUnit>();
			});

		registry.RegisterUnit("Verweilzeit: Drift/Diffusion FPT (stochastisch, Partikel)", []()
			{
				return std::make_unique<CFptDriftStochasticParticleUnit>();
			});

		registry.RegisterUnit("Verweilzeit: Drift/Diffusion FPT (stochastisch, GPU)", []()
			{
				return std::make_unique<CFptDriftStochasticGpuUnit>();
			});
	}
}