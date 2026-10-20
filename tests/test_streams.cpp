// Stroeme mit Stoffen, Temperatur und Druck (Methoden-Dokument Kapitel 14.4,
// Pruefung Schritt 3).
#include "FlowsheetGraph.h"
#include <cstdio>
#include <cmath>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

static bool Near(double a, double b, double tol) { return NearAbs(a, b, tol); }

static const std::vector<SInletTimePoint> Constant(double massFlow) { return { {0.0, massFlow}, {100.0, massFlow} }; }

// Inlet A + Inlet B -> Merger -> Outlet
struct SMix
{
    CFlowsheetGraph graph;
    std::string a, b, merger, outlet;
    SMix(const CMaterialDatabase* db, const std::string& matA, double flowA, double TA, double pA,
         const std::string& matB, double flowB, double TB, double pB)
    {
        graph.SetMaterialDatabase(db);
        a = graph.AddInletNode("InletA", 0, 0);
        b = graph.AddInletNode("InletB", 0, 1);
        merger = graph.AddMergerNode("Merger1", 2, 1, 0);
        outlet = graph.AddOutletNode("Outlet1", 2, 0);
        for (auto [id, mat, flow, T, p] : { std::tuple{a, matA, flowA, TA, pA}, std::tuple{b, matB, flowB, TB, pB} })
        {
            CInletUnit* inlet = graph.GetInlet(id);
            inlet->SetMaterial(mat);
            inlet->SetTimeProfile(Constant(flow));
            inlet->SetTemperature(T);
            inlet->SetPressure(p);
        }
        graph.AddConnection(a, 0, merger, 0);
        graph.AddConnection(b, 0, merger, 1);
        graph.AddConnection(merger, 0, outlet, 0);
        graph.ConfigureGrid(150, 0.0, 200e-6, 50, 10.0, ESizeGridType::Linear);
        graph.RunSimulations(1);
    }
    const CStreamRecord& Out() { return *graph.GetDisplayRecord(outlet, 0); }
};

int main()
{
    CMaterialDatabase db;
    std::string error;
    if (!db.LoadFromFile(DataFile("Materialien.json"), error))
    {
        std::printf("Materialdatei: %s\n", error.c_str());
        return 1;
    }

    // 1) Massenbilanz je Stoff: Sand + Coal -> Merger -> Splitter -> 2 Outlets.
    {
        CFlowsheetGraph graph;
        graph.SetMaterialDatabase(&db);
        const std::string sand = graph.AddInletNode("Inlet1", 0, 0);
        const std::string coal = graph.AddInletNode("Inlet2", 0, 1);
        const std::string mg = graph.AddMergerNode("Merger1", 2, 1, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 2, 0);
        const std::string o1 = graph.AddOutletNode("Outlet1", 3, 0);
        const std::string o2 = graph.AddOutletNode("Outlet2", 3, 1);
        graph.GetInlet(coal)->SetMaterial("Coal");
        graph.GetInlet(coal)->SetTimeProfile(Constant(5.0));
        graph.GetInlet(coal)->SetSizeDistribution(60e-6, 10e-6);
        graph.GetSplitter(sp)->SetFractions({ 0.3, 0.7 });
        graph.AddConnection(sand, 0, mg, 0);
        graph.AddConnection(coal, 0, mg, 1);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, o1, 0);
        graph.AddConnection(sp, 1, o2, 0);
        graph.ConfigureGrid(150, 0.0, 200e-6, 200, 40.0, ESizeGridType::Linear);
        graph.RunSimulations(1);

        const std::vector<std::string> active = graph.GetActiveCompounds();
        Check(active.size() == 2 && active[0] == "Coal" && active[1] == "Sand", "Aktive Stoffe: Coal, Sand (Reihenfolge der Materialdatei)");

        const CStreamRecord& out1 = *graph.GetDisplayRecord(o1, 0);
        const CStreamRecord& out2 = *graph.GetDisplayRecord(o2, 0);
        bool balance = true;
        for (size_t c = 0; c < 2; ++c)
        {
            const double in = graph.GetOutputRecord(c == 0 ? coal : sand, 0)->GetCompound(c).IntegrateTotal();
            const double outSum = out1.GetCompound(c).IntegrateTotal() + out2.GetCompound(c).IntegrateTotal();
            std::printf("      %s: ein %.10g kg, aus %.10g kg (rel. %.1e)\n", active[c].c_str(), in, outSum, std::abs(outSum - in) / in);
            balance = balance && std::abs(outSum - in) <= 1e-12 * in;
            const double other = graph.GetOutputRecord(c == 0 ? sand : coal, 0)->GetCompound(c).IntegrateTotal();
            balance = balance && other == 0.0; // jedes Inlet liefert nur seinen Stoff
        }
        Check(balance, "Massenbilanz je Stoff exakt (rel. 1e-12), Inlets liefern nur ihren Stoff");

        const double total = out1.GetTotal().IntegrateTotal();
        const double sum = out1.GetCompound(0).IntegrateTotal() + out1.GetCompound(1).IntegrateTotal();
        Check(std::abs(total - sum) <= 1e-12 * total, "Gesamtmenge = Summe der Stoffe");
        Check(std::abs(out1.GetCompound(0).IntegrateTotal() / (out1.GetCompound(0).IntegrateTotal() + out2.GetCompound(0).IntegrateTotal()) - 0.3) < 1e-12,
            "Splitter teilt jeden Stoff 30/70");

        const CStreamStatistics* stats = graph.GetStreamStatistics(o1, 0);
        Check(stats && stats->GetNumCompounds() == 2 && stats->GetCompound(1).GetRunTotals().size() == 1, "Statistik je Stoff vorhanden");
    }

    // 2) Mischtemperatur gleicher Stoff: 10 kg/s Sand bei 20 C + 5 kg/s bei 80 C -> 40 C.
    {
        SMix mix(&db, "Sand", 10.0, 293.15, 101325.0, "Sand", 5.0, 353.15, 101325.0);
        const double T = mix.Out().GetTemperature()[5];
        std::printf("      Sand + Sand: %.9f K = %.6f C\n", T, T - 273.15);
        Check(Near(T, 313.15, 1e-9), "Sand 20 C + Sand 80 C (10:5) -> 40 C");
        Check(mix.graph.GetSimulationWarnings().empty(), "keine Hinweise");
    }

    // 3) Verschiedene Stoffe: Sand (830) + Harnstoff (1544.7) -> etwa 48.9 C.
    {
        SMix mix(&db, "Sand", 10.0, 293.15, 101325.0, "Urea", 5.0, 353.15, 101325.0);
        const double expected = (10 * 830 * 293.15 + 5 * 1544.7 * 353.15) / (10 * 830 + 5 * 1544.7);
        const double T = mix.Out().GetTemperature()[5];
        std::printf("      Sand + Harnstoff: %.9f K = %.4f C (erwartet %.9f K)\n", T, T - 273.15, expected);
        Check(Near(T, expected, 1e-8), "Sand 20 C + Harnstoff 80 C -> Energiebilanz (48.9 C)");
    }

    // 4) Druck: kleinster Eingangsdruck; Eingang ohne Massenstrom zaehlt nicht.
    {
        SMix both(&db, "Sand", 10.0, 293.15, 2.0e5, "Sand", 5.0, 293.15, 1.0e5);
        Check(both.Out().GetPressure()[5] == 1.0e5, "2 bar + 1 bar -> 1 bar");
        SMix empty(&db, "Sand", 10.0, 293.15, 2.0e5, "Sand", 0.0, 293.15, 1.0e5);
        Check(empty.Out().GetPressure()[5] == 2.0e5, "Eingang ohne Massenstrom zaehlt nicht -> 2 bar");
        Check(both.Out().GetTemperature()[5] == 293.15, "gleiche Temperatur bleibt exakt erhalten");
    }

    // 5) Stoff ohne Waermekapazitaet: Hinweis und Massenmittel.
    {
        SMix mix(&db, "Sand", 10.0, 293.15, 101325.0, "Coal", 5.0, 353.15, 101325.0);
        const double T = mix.Out().GetTemperature()[5];
        const std::vector<std::string> warnings = mix.graph.GetSimulationWarnings();
        for (const std::string& w : warnings)
            std::printf("      Hinweis: %s\n", w.c_str());
        Check(Near(T, 313.15, 1e-9) && warnings.size() == 1 && warnings[0].find("Coal") != std::string::npos,
            "Coal ohne c_p: Hinweis, Temperatur nach Masse gewichtet");
    }

    // 6) Ohne Materialdatei: rechnet trotzdem, mischt nach Masse.
    {
        SMix mix(nullptr, "Sand", 10.0, 293.15, 101325.0, "Urea", 5.0, 353.15, 101325.0);
        Check(Near(mix.Out().GetTemperature()[5], 313.15, 1e-9) && mix.graph.GetSimulationWarnings().size() == 2,
            "Ohne Materialdatei: Massenmittel und Hinweise");
    }

    // 7) Temperatur wandert mit dem Material durch die Verzoegerung.
    {
        CFlowsheetGraph graph;
        graph.SetMaterialDatabase(&db);
        const std::string a = graph.AddInletNode("InletA", 0, 0);
        const std::string b = graph.AddInletNode("InletB", 0, 1);
        const std::string mg = graph.AddMergerNode("Merger1", 2, 1, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 2, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 3, 0);
        graph.GetInlet(a)->SetTimeProfile(Constant(10.0));
        graph.GetInlet(b)->SetTimeProfile({ {0.0, 0.0}, {40.0, 20.0} }); // waechst -> Mischtemperatur steigt
        graph.GetInlet(b)->SetTemperature(353.15);
        graph.GetProcessor(pr)->SetParameter("delayRef", 2.0);
        graph.GetProcessor(pr)->SetParameter("sizeExponent", 0.0); // alle Groessen gleich lang unterwegs
        graph.AddConnection(a, 0, mg, 0);
        graph.AddConnection(b, 0, mg, 1);
        graph.AddConnection(mg, 0, pr, 0);
        graph.AddConnection(pr, 0, out, 0);
        graph.ConfigureGrid(150, 0.0, 200e-6, 201, 40.0, ESizeGridType::Linear); // dt = 0.2 s -> 2 s = 10 Schritte
        graph.RunSimulations(1);
        const std::vector<double>& Tin = graph.GetOutputRecord(mg, 0)->GetTemperature();
        const std::vector<double>& Tout = graph.GetDisplayRecord(out, 0)->GetTemperature();
        double maxDiff = 0.0;
        for (size_t k = 10; k < Tin.size(); ++k)
            maxDiff = std::max(maxDiff, std::abs(Tout[k] - Tin[k - 10]));
        std::printf("      T_Merger: %.3f K (t=2 s) ... %.3f K (t=40 s); max |T_aus(t) - T_ein(t - 2 s)| = %.2e K\n",
            Tin[10], Tin.back(), maxDiff);
        Check(Tin.back() > Tin[10] + 10.0 && maxDiff < 1e-6, "Austrittstemperatur = Eintrittstemperatur 2 s frueher");
    }

    // 8) Nur ein Stoff: keine eigene Stoff-Matrix, Stoff 0 = Gesamt.
    {
        CFlowsheetGraph graph;
        graph.SetMaterialDatabase(&db);
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 1, 0);
        graph.AddConnection(in, out);
        graph.ConfigureGrid(50, 0.0, 200e-6, 20, 10.0, ESizeGridType::Linear);
        graph.RunSimulations(1);
        const CStreamRecord& rec = *graph.GetDisplayRecord(out, 0);
        Check(rec.GetNumCompounds() == 1 && &rec.GetCompound(0) == &rec.GetTotal() && rec.GetCompoundNames().size() == 1
            && rec.GetCompoundNames()[0] == "Sand" && rec.GetTemperature()[3] == kRoomTemperature && rec.GetPressure()[3] == kRoomPressure,
            "Ein Stoff (Sand, Raumbedingungen): Stoff 0 ist die Gesamtmenge");
    }

    return Summary();
}
