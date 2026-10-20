#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "TransportUnit.h"

namespace stosis {


    class CTransportUnitRegistry
    {
    public:
        using FactoryFunc = std::function<std::unique_ptr<ITransportUnit>()>;

        void RegisterUnit(const std::string& typeName, FactoryFunc factory)
        {
            m_factories.emplace_back(typeName, std::move(factory));
        }

        std::vector<std::string> GetAvailableTypeNames() const
        {
            std::vector<std::string> names;
            for (const auto& entry : m_factories)
                names.push_back(entry.first);
            return names;
        }

        std::unique_ptr<ITransportUnit> CreateUnit(const std::string& typeName) const
        {
            for (const auto& entry : m_factories)
                if (entry.first == typeName)
                    return entry.second();
            return nullptr;
        }

    private:
        std::vector<std::pair<std::string, FactoryFunc>> m_factories;
    };


}