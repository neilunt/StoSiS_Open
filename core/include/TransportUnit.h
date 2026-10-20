#pragma once


#include <vector>
#include <string>
#include "Stream.h"
#include "SimulationResults.h"
#include "ParameterDescriptor.h"

namespace stosis {


    /*struct SParameterDescriptor
    {
        std::string id;
        std::string label;
        double minValue;
        double maxValue;
        double defaultValue;
    };*/


    class ITransportUnit
    {
    public:
        virtual ~ITransportUnit() = default;

        virtual std::string GetTypeName() const = 0;
        virtual std::string GetResultLabel() const = 0;
        virtual std::vector<SParameterDescriptor> GetParameterDescriptors() const = 0;
        virtual void SetParameter(const std::string& id, double value) = 0;

        virtual void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax,
            size_t numTimeSteps, double totalTime) = 0;

        virtual void SetInputStream(CStream* input) = 0;
        virtual CStream* GetOutputStream() = 0;

        virtual void Simulate() = 0;

        virtual const CSimulationResults& GetResults() const = 0;
    };

}