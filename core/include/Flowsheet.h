#pragma once
#include <memory>
#include <string>
#include <vector>
#include "DistributionSourceUnit.h"
#include "TransportUnit.h"
#include "TransportUnitRegistry.h"
#include "BuiltinTransportUnits.h"
#include "SimulationResults.h"

namespace stosis
{
    class CFlowsheet
    {
    public:
        CFlowsheet();

        const CTransportUnitRegistry& GetTransportUnitRegistry() const { return m_registry; }

        void SelectTransportUnit(const std::string& typeName);
        std::vector<SParameterDescriptor> GetTransportUnitParameters() const;
        void SetTransportUnitParameter(const std::string& id, double value);

        void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
            size_t numTimeSteps, double totalTime);
        void SetInitialGaussian(double d0, double sigma0, double totalMass);

        void RunDistributedSimulation();
        const CSimulationResults& GetDistributedResults() const;

        std::string GetTransportUnitResultLabel() const;

    private:
        CTransportUnitRegistry m_registry;
        std::unique_ptr<CDistributionSourceUnit> m_distSource;
        std::unique_ptr<ITransportUnit> m_transportUnit;

        size_t m_numClasses = 150;
        double m_sizeMin = 0.0;
        double m_sizeMax = 200.0;
        size_t m_numTimeSteps = 200;
        double m_totalTime = 10.0;
    };
}