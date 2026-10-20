#pragma once
#include <memory>
#include "StreamProcessorRegistry.h"
#include "SizeDependentDelayProcessor.h"

namespace stosis
{
	// Trägt alle eingebauten StreamProzessoren in die Registry ein
	// Analog zu RegisterBuiltinTransportUnits.
	inline void RegisterBuiltinStreamProcessors(CStreamProcessorRegistry& registry)
	{
		registry.RegisterProcessor("Verzoegerung: Groessenabhaengig", []()
			{
				return std::make_unique<CSizeDependentDelayProcessor>();
			});
	}
}