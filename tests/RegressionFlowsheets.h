// The fixed flowsheets of the regression test (test_regression) and of the
// comparison of continuum and particle mode (test_particle_vs_continuum,
// methods handbook 9.7). Each builder sets up a fresh graph without running
// it; runs is the number of runs the case gets. The order of the nodes
// fixes their IDs and with them the random streams, so it must not change.
#pragma once
#include "FlowsheetGraph.h"

#include <functional>
#include <string>
#include <vector>

namespace stosis_test
{
    struct SRegressionCase
    {
        std::string name;
        size_t runs = 1;
        std::function<void(stosis::CFlowsheetGraph&)> build;
    };

    // Inlet -> groessenabhaengige Verzoegerung -> Outlet mit wechselndem Zulauf.
    inline void BuildChain(stosis::CFlowsheetGraph& g, double delay, double exponent, size_t numTimeSteps, double totalTime)
    {
        using namespace stosis;
        const std::string in = g.AddInletNode("In", 0, 0);
        const std::string pr = g.AddProcessNode("P", 0, 0);
        const std::string out = g.AddOutletNode("Out", 0, 0);
        g.AddConnection(in, pr);
        g.AddConnection(pr, out);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10}, {12, 0}, {18, 10}, {20, 0}, {30, 20}, {33, 10} });
        g.GetInlet(in)->SetSizeDistribution(60e-6, 5e-6);
        g.GetProcessor(pr)->SetParameter("delayRef", delay);
        g.GetProcessor(pr)->SetParameter("sizeExponent", exponent);
        g.GetProcessor(pr)->SetParameter("referenceSize", 60e-6);
        g.ConfigureGrid(150, 0.0, 200e-6, numTimeSteps, totalTime, ESizeGridType::Linear);
    }

    inline std::vector<SRegressionCase> RegressionCases()
    {
        using namespace stosis;
        std::vector<SRegressionCase> cases;

        // Einfaches Flowsheet Inlet -> Verzoegerung -> Outlet, Standardwerte.
        cases.push_back({ "Start-Flowsheet", 20, [](CFlowsheetGraph& graph)
        {
            const std::string in = graph.AddInletNode("Inlet1", 0, 0);
            const std::string pr = graph.AddProcessNode("Prozess1", 1, 0);
            const std::string out = graph.AddOutletNode("Outlet1", 2, 0);
            graph.AddConnection(in, pr);
            graph.AddConnection(pr, out);
            graph.ConfigureGrid(150, 0.0, 200e-6, 200, 40.0, ESizeGridType::Linear);
        } });

        // Zwei Inlets, Merger, stochastischer 3er-Splitter, Rueckfuehrung ueber Verzoegerung.
        cases.push_back({ "Kreis stochastisch", 20, [](CFlowsheetGraph& graph)
        {
            const std::string in1 = graph.AddInletNode("Inlet1", 0, 0);
            const std::string in2 = graph.AddInletNode("Inlet2", 0, 1);
            const std::string mg = graph.AddMergerNode("Merger1", 3, 1, 0);
            const std::string sp = graph.AddSplitterNode("Splitter1", 3, 2, 0);
            const std::string pr = graph.AddProcessNode("Prozess1", 3, 1);
            const std::string o1 = graph.AddOutletNode("Outlet1", 4, 0);
            const std::string o2 = graph.AddOutletNode("Outlet2", 4, 1);
            graph.GetInlet(in2)->SetTimeProfile({ {0.0, 5.0}, {40.0, 5.0} });
            graph.GetInlet(in2)->SetSizeDistribution(60e-6, 10e-6);
            graph.GetProcessor(pr)->SetParameter("delayRef", 3.0);
            graph.GetProcessor(pr)->SetParameter("sizeExponent", 1.0);
            CSplitterUnit* splitter = graph.GetSplitter(sp);
            splitter->SetFractions({ 0.5, 0.3, 0.2 });
            splitter->SetStochastic(true);
            splitter->SetFluctuations({ {0.08, 1.5}, {0.05, 0.5} });
            graph.AddConnection(in1, 0, mg, 0);
            graph.AddConnection(in2, 0, mg, 1);
            graph.AddConnection(mg, 0, sp, 0);
            graph.AddConnection(sp, 0, o1, 0);
            graph.AddConnection(sp, 1, pr, 0);
            graph.AddConnection(pr, 0, mg, 2);
            graph.AddConnection(sp, 2, o2, 0);
            graph.ConfigureGrid(120, 0.0, 200e-6, 250, 40.0, ESizeGridType::Linear);
            graph.SetRandomSeed(7);
        } });

        cases.push_back({ "Kette d2 p1", 1, [](CFlowsheetGraph& g) { BuildChain(g, 2.0, 1.0, 200, 10.0); } });
        cases.push_back({ "Kette d0.37 p-1", 1, [](CFlowsheetGraph& g) { BuildChain(g, 0.37, -1.0, 800, 40.0); } });
        cases.push_back({ "Kette d0 p1", 1, [](CFlowsheetGraph& g) { BuildChain(g, 0.0, 1.0, 200, 10.0); } });
        cases.push_back({ "Kette d5 p3", 1, [](CFlowsheetGraph& g) { BuildChain(g, 5.0, 3.0, 300, 40.0); } });

        // Splitter 20/30/50 und Merger, RRSB-Verteilung.
        cases.push_back({ "Splitter/Merger RRSB", 1, [](CFlowsheetGraph& g)
        {
            const std::string in = g.AddInletNode("In", 0, 0);
            const std::string sp = g.AddSplitterNode("Split", 3, 0, 0);
            const std::string mg = g.AddMergerNode("Merge", 2, 0, 0);
            const std::string oa = g.AddOutletNode("A", 0, 0);
            const std::string ob = g.AddOutletNode("B", 0, 0);
            g.GetInlet(in)->SetTimeProfile({ {0, 0}, {2, 10}, {4, 0}, {6, 20}, {7, 10} });
            g.GetInlet(in)->SetSizeDistribution(SSizeDistribution{ EDistributionType::RRSB, 65e-6, 8 });
            g.GetSplitter(sp)->SetFractions({ 0.2, 0.3, 0.5 });
            g.AddConnection(in, 0, sp, 0);
            g.AddConnection(sp, 0, oa, 0);
            g.AddConnection(sp, 1, mg, 0);
            g.AddConnection(sp, 2, mg, 1);
            g.AddConnection(mg, 0, ob, 0);
            // Ausdruecklich lineares Raster.
            g.ConfigureGrid(150, 0.0, 200e-6, 200, 10.0, ESizeGridType::Linear);
        } });

        // Logarithmisches Raster (Standard): einfaches Flowsheet mit
        // Standardwerten und Rueckfuehrung mit stochastischem Splitter.
        cases.push_back({ "Log Start-Flowsheet", 20, [](CFlowsheetGraph& graph)
        {
            const std::string in = graph.AddInletNode("Inlet1", 0, 0);
            const std::string pr = graph.AddProcessNode("Prozess1", 1, 0);
            const std::string out = graph.AddOutletNode("Outlet1", 2, 0);
            graph.AddConnection(in, pr);
            graph.AddConnection(pr, out);
            graph.ConfigureGrid(300, 1e-6, 1e-3, 200, 40.0);
        } });

        // Start-Flowsheet des Programms (MainWindow::BuildTestFlowsheet; dort mit
        // dt = 0,2 s, also 201 statt hier 200 Zeitpunkten):
        // Inlet -> Merger -> Splitter 70/30 -> Outlet, 30 % ueber die
        // Verzoegerung zurueck zum Merger.
        cases.push_back({ "Log Start-Flowsheet Rueckfuehrung", 20, [](CFlowsheetGraph& graph)
        {
            const std::string in = graph.AddInletNode("Inlet1", 0, 55);
            const std::string mg = graph.AddMergerNode("Merger1", 2, 160, 60);
            const std::string sp = graph.AddSplitterNode("Splitter1", 2, 320, 60);
            const std::string out = graph.AddOutletNode("Outlet1", 500, 0);
            const std::string pr = graph.AddProcessNode("Prozess1", 500, 120);
            graph.GetSplitter(sp)->SetFractions({ 0.7, 0.3 });
            graph.AddConnection(in, 0, mg, 0);
            graph.AddConnection(mg, 0, sp, 0);
            graph.AddConnection(sp, 0, out, 0);
            graph.AddConnection(sp, 1, pr, 0);
            graph.AddConnection(pr, 0, mg, 1);
            graph.ConfigureGrid(300, 1e-6, 1e-3, 200, 40.0);
        } });

        cases.push_back({ "Log Kreis stochastisch", 20, [](CFlowsheetGraph& graph)
        {
            const std::string in1 = graph.AddInletNode("Inlet1", 0, 0);
            const std::string in2 = graph.AddInletNode("Inlet2", 0, 1);
            const std::string mg = graph.AddMergerNode("Merger1", 3, 1, 0);
            const std::string sp = graph.AddSplitterNode("Splitter1", 2, 2, 0);
            const std::string pr = graph.AddProcessNode("Prozess1", 3, 1);
            const std::string o1 = graph.AddOutletNode("Outlet1", 4, 0);
            graph.GetInlet(in2)->SetTimeProfile({ {0.0, 5.0}, {40.0, 5.0} });
            graph.GetInlet(in2)->SetSizeDistribution(SSizeDistribution{ EDistributionType::LogNormal, 10e-6, 0.8 });
            graph.GetProcessor(pr)->SetParameter("delayRef", 3.0);
            graph.GetProcessor(pr)->SetParameter("sizeExponent", -0.5);
            CSplitterUnit* splitter = graph.GetSplitter(sp);
            splitter->SetFractions({ 0.6, 0.4 });
            splitter->SetStochastic(true);
            splitter->SetFluctuations({ {0.08, 1.5} });
            graph.AddConnection(in1, 0, mg, 0);
            graph.AddConnection(in2, 0, mg, 1);
            graph.AddConnection(mg, 0, sp, 0);
            graph.AddConnection(sp, 0, o1, 0);
            graph.AddConnection(sp, 1, pr, 0);
            graph.AddConnection(pr, 0, mg, 2);
            graph.ConfigureGrid(400, 0.1e-6, 1e-3, 250, 40.0);
            graph.SetRandomSeed(7);
        } });

        return cases;
    }
}
