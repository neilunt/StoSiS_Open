#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "StreamProcessor.h"

namespace stosis
{
	// Registry für verfügbare Streamprozessoren, 
	// analog zu CTransportUnitRegistry
	class CStreamProcessorRegistry
	{
	public:
		using FactoryFunc = std::function < std::unique_ptr<IStreamProcessor>()>;

		void RegisterProcessor(const std::string& typeName, FactoryFunc factory)
		{
			m_factories.emplace_back(typeName, std::move(factory));
		}

		std::vector<std::string> GetAvailableTypeNames() const
		{
			std::vector<std::string> names;
			names.reserve(m_factories.size());
			for (const auto& entry : m_factories)
				names.push_back(entry.first);
			return names;
		}

		std::unique_ptr<IStreamProcessor> CreateProcessor(const std::string& typeName)
		{
			for (const auto& entry : m_factories)
				if (entry.first == typeName)
					return entry.second();
			return nullptr;
		}

	private:
		std::vector < std::pair<std::string, FactoryFunc>> m_factories;
	};
}