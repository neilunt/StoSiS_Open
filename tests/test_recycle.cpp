// Rueckfuehrungen: Aufschneiden von Kreisen, stationaere Werte,
// Einschwingen und Massenbilanz (Methoden-Dokument Kapitel 6.3).
#include "FlowsheetGraph.h"
#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Massenstrom im letzten Zeitschritt.
static double Last(const CSimulationResults* r) { return r->IntegrateOverSize(r->GetNumTimeSteps() - 1); }

int main()
{
    // 1) Inlet (konstant 10) -> Merger -> Splitter 50/50 -> Outlet, Ausgang 2
    //    zurueck zum Merger. Stationaer: Merger 20, Outlet 10, Rueckfuehrung 10;
    //    Einschwingen M_k = 10 + M_(k-1)/2 = 10, 15, 17.5, 18.75, 19.375.
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string out = g.AddOutletNode("Outlet1", 0, 0); // absichtlich vor dem Kreis angelegt
        const std::string mg = g.AddMergerNode("Merger1", 2, 0, 0);
        const std::string sp = g.AddSplitterNode("Splitter1", 2, 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 10} });
        g.GetInlet(in)->SetSizeDistribution(60e-6, 5e-6);
        g.AddConnection(in, 0, mg, 0);
        g.AddConnection(mg, 0, sp, 0);
        g.AddConnection(sp, 0, out, 0);
        const std::string recycle = g.AddConnection(sp, 1, mg, 1);
        g.ConfigureGrid(150, 0.0, 200e-6, 201, 10, ESizeGridType::Linear);
        g.RunSimulation();

        const std::set<std::string> lagged = g.GetLaggedConnectionIds();
        Check(lagged.size() == 1 && *lagged.begin() == recycle, "Kreis 50/50: aufgeschnitten wird Splitter -> Merger");

        const CSimulationResults* m = g.GetDisplayResults(mg, 0);
        const double feed = m->IntegrateOverSize(0); // Zulauf 10 als Bezug (Raster erfasst die Verteilung nicht exakt)
        bool settling = true;
        double expected = 0.0;
        std::printf("      Merger erste Schritte:");
        for (size_t k = 0; k < 5; ++k)
        {
            expected = feed + 0.5 * expected;
            std::printf(" %.4f", m->IntegrateOverSize(k));
            settling = settling && NearRel(m->IntegrateOverSize(k), expected, 1e-12);
        }
        std::printf("\n");
        Check(NearRel(feed, 10.0, 1e-3), "Zulauf 10 kg/s");
        Check(settling, "Einschwingen 10, 15, 17.5, 18.75, 19.375 (je mal Zulauf/10)");
        Check(NearRel(Last(m), 2.0 * feed, 1e-12) && NearRel(Last(g.GetDisplayResults(out, 0)), feed, 1e-12)
            && NearRel(Last(g.GetDisplayResults(sp, 1)), feed, 1e-12),
            "Stationaer: Merger 20, Outlet 10, Rueckfuehrung 10");
    }

    // 2) Kreis mit Verzoegerung 1 s und Splitter 70/30 (30 % zurueck):
    //    stationaer Merger = 10/0.7 = 14.2857, Outlet = 10. Massenbilanz:
    //    rein - raus = Hold-up = Merger * 1 s + Rueckfuehrung * dt.
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string mg = g.AddMergerNode("Merger1", 2, 0, 0);
        const std::string pr = g.AddProcessNode("Prozess1", 0, 0);
        const std::string sp = g.AddSplitterNode("Splitter1", 2, 0, 0);
        const std::string out = g.AddOutletNode("Outlet1", 0, 0);
        g.GetInlet(in)->SetTimeProfile({ {0, 0}, {1, 10} });
        g.GetInlet(in)->SetSizeDistribution(60e-6, 5e-6);
        g.GetProcessor(pr)->SetParameter("delayRef", 1.0);
        g.GetProcessor(pr)->SetParameter("sizeExponent", 0.0);
        g.GetSplitter(sp)->SetFractions({ 0.7, 0.3 });
        g.AddConnection(in, 0, mg, 0);
        g.AddConnection(mg, 0, pr, 0);
        g.AddConnection(pr, 0, sp, 0);
        g.AddConnection(sp, 0, out, 0);
        g.AddConnection(sp, 1, mg, 1);
        g.ConfigureGrid(150, 0.0, 200e-6, 401, 40, ESizeGridType::Linear);
        g.RunSimulation();

        const double feed = Last(g.GetDisplayResults(in, 0));
        const double merger = Last(g.GetDisplayResults(mg, 0));
        const double outlet = Last(g.GetDisplayResults(out, 0));
        std::printf("      Zulauf %.5f | Merger %.5f | Outlet %.5f\n", feed, merger, outlet);
        Check(NearRel(merger, feed / 0.7, 1e-9) && NearRel(outlet, feed, 1e-9), "Stationaer: Merger = Zulauf/0.7, Outlet = Zulauf");

        const double mIn = g.GetDisplayResults(in, 0)->IntegrateTotal();
        const double mOut = g.GetDisplayResults(out, 0)->IntegrateTotal();
        const double holdUp = merger * 1.0 + 0.3 * merger * g.GetGrid().GetDt();
        std::printf("      rein %.4f, raus %.4f, Differenz %.4f, Hold-up %.4f\n", mIn, mOut, mIn - mOut, holdUp);
        Check(NearRel(mIn - mOut, holdUp, 0.01), "Massenbilanz: rein - raus = Hold-up (Verzoegerung + Rohr dt), 1 %");
    }

    // 3) Manuell markierte Rueckfuehrung hat Vorrang.
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string mg = g.AddMergerNode("Merger1", 2, 0, 0);
        const std::string sp = g.AddSplitterNode("Splitter1", 2, 0, 0);
        const std::string out = g.AddOutletNode("Outlet1", 0, 0);
        g.AddConnection(in, 0, mg, 0);
        const std::string marked = g.AddConnection(mg, 0, sp, 0);
        g.AddConnection(sp, 0, out, 0);
        g.AddConnection(sp, 1, mg, 1);
        g.SetConnectionRecycle(marked, true);
        const std::set<std::string> lagged = g.GetLaggedConnectionIds();
        Check(lagged.size() == 1 && *lagged.begin() == marked, "Manuell markierte Verbindung wird geschnitten");
    }

    // 4) Ohne Kreis wird nichts aufgeschnitten.
    {
        CFlowsheetGraph g;
        const std::string in = g.AddInletNode("Inlet1", 0, 0);
        const std::string out = g.AddOutletNode("Outlet1", 0, 0);
        g.AddConnection(in, out);
        Check(g.GetLaggedConnectionIds().empty(), "Ohne Kreis: keine Schnittstelle");
    }

    return Summary();
}
