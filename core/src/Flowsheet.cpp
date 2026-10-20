#include "Flowsheet.h"

namespace stosis
{
    CFlowsheet::CFlowsheet()
        : m_distSource(std::make_unique<CDistributionSourceUnit>("DistInput"))
    {
        RegisterBuiltinTransportUnits(m_registry);

        const auto names = m_registry.GetAvailableTypeNames();
        if (!names.empty())
            SelectTransportUnit(names.front());
    }

    void CFlowsheet::SelectTransportUnit(const std::string& typeName)
    {
        m_transportUnit = m_registry.CreateUnit(typeName);
        if (m_transportUnit)
        {
            m_transportUnit->ConfigureGrid(m_numClasses, m_sizeMin, m_sizeMax, m_numTimeSteps, m_totalTime);
            m_transportUnit->SetInputStream(m_distSource->GetOutputStream());
        }
    }

    std::vector<SParameterDescriptor> CFlowsheet::GetTransportUnitParameters() const
    {
        if (m_transportUnit)
            return m_transportUnit->GetParameterDescriptors();
        return {};
    }

    void CFlowsheet::SetTransportUnitParameter(const std::string& id, double value)
    {
        if (m_transportUnit)
            m_transportUnit->SetParameter(id, value);
    }

    void CFlowsheet::ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
        size_t numTimeSteps, double totalTime)
    {
        m_numClasses = numClasses;
        m_sizeMin = sizeMin;
        m_sizeMax = sizeMax;
        m_numTimeSteps = numTimeSteps;
        m_totalTime = totalTime;

        m_distSource->ConfigureGrid(numClasses, sizeMin, sizeMax);
        if (m_transportUnit)
            m_transportUnit->ConfigureGrid(numClasses, sizeMin, sizeMax, numTimeSteps, totalTime);
    }

    void CFlowsheet::SetInitialGaussian(double d0, double sigma0, double totalMass)
    {
        m_distSource->SetInitialGaussian(d0, sigma0, totalMass);
    }

    void CFlowsheet::RunDistributedSimulation()
    {
        m_distSource->Simulate(0.0, 0.0);
        if (m_transportUnit)
            m_transportUnit->Simulate();
    }

    const CSimulationResults& CFlowsheet::GetDistributedResults() const
    {
        static CSimulationResults empty;
        return m_transportUnit ? m_transportUnit->GetResults() : empty;
    }

    std::string CFlowsheet::GetTransportUnitResultLabel() const
    {
        return m_transportUnit ? m_transportUnit->GetResultLabel() : "";
    }
}