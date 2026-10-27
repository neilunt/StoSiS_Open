// Merger und Splitter im Partikelmodus (Methoden-Dokument 13.18, 13.22):
// Karten aneinanderhaengen, Konto-Verfahren, Vergleich mit dem Kontinuum.
#include "FlowsheetGraph.h"
#include "CardPlacement.h"
#include "SignalAnalysis.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Karten einer Verteilung fuer einen Stoff, skaliert auf massFlow.
static void AddCards(CStreamSlice& slice, const SSizeDistribution& distribution, size_t numCards,
    size_t compound, double massFlow)
{
    SCardSettings settings;
    settings.cardsPerSlice = numCards;
    const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6, 1e-3);
    for (size_t k = 0; k < placement.sizes.size(); ++k)
        slice.AddCard(placement.sizes[k], compound, massFlow * placement.massFractions[k]);
}

static SSimulationGrid MakeGrid(size_t numCompounds, size_t numTimeSteps, double totalTime)
{
    SSimulationGrid grid;
    grid.representation = ERepresentation::Particles;
    grid.numTimeSteps = numTimeSteps;
    grid.totalTime = totalTime;
    grid.compounds.clear();
    for (size_t c = 0; c < numCompounds; ++c)
        grid.compounds.push_back(c == 0 ? "Sand" : "Coal");
    return grid;
}

static double MaxCardMass(const CStreamSlice& slice)
{
    double m = 0.0;
    for (size_t k = 0; k < slice.GetNumCards(); ++k)
        m = std::max(m, slice.GetCardMassFlow(k));
    return m;
}

// Massengewichtete mittlere Groesse der Karten einer Scheibe.
static double MeanSize(const CStreamSlice& slice)
{
    double mass = 0.0, moment = 0.0;
    for (size_t k = 0; k < slice.GetNumCards(); ++k)
    {
        mass += slice.GetCardMassFlow(k);
        moment += slice.GetCardMassFlow(k) * slice.GetCardSize(k);
    }
    return mass > 0.0 ? moment / mass : 0.0;
}

int main()
{
    const SSizeDistribution standard; // Log-Normal 100 um / 0,25

    // 1) Merger: haengt die Karten aneinander, Masse je Stoff exakt, Temperatur
    //    aus der Mischung (ohne Materialdatei nach Masse gewichtet).
    {
        const SSimulationGrid grid = MakeGrid(2, 11, 1.0);
        CMergerUnit merger(2);
        merger.Initialize(grid, 1);
        CStreamSlice sand, coal;
        sand.Resize(300, 2, ERepresentation::Particles);
        coal.Resize(300, 2, ERepresentation::Particles);
        AddCards(sand, standard, 2000, 0, 3.0);
        AddCards(coal, SSizeDistribution{ EDistributionType::RRSB, 65e-6, 8.0 }, 2000, 1, 1.0);
        sand.SetTemperature(300.0);
        coal.SetTemperature(400.0);
        std::vector<CStreamSlice> outputs(1);
        outputs[0].Resize(300, 2, ERepresentation::Particles);
        merger.Step({ 1, 0.1, 0.1 }, { &sand, &coal }, outputs);
        const CStreamSlice& out = outputs[0];
        Check(out.GetNumCards() == 4000, "Merger: 2000 + 2000 Karten = 4000 Karten");
        Check(NearRel(out.GetCardsMassFlow(0), 3.0, 1e-12) && NearRel(out.GetCardsMassFlow(1), 1.0, 1e-12),
            "Merger: Massenstrom je Stoff exakt (3 kg/s Sand, 1 kg/s Kohle)");
        Check(NearAbs(out.GetTemperature(), (3.0 * 300.0 + 1.0 * 400.0) / 4.0, 1e-9),
            "Merger: Mischtemperatur 325 K (nach Masse, ohne Materialdatei)");
    }

    // 2) Splitter, fester Anteil 30 / 70, 2000 Karten je Schritt, 1000 Schritte.
    {
        const size_t steps = 1000;
        const SSimulationGrid grid = MakeGrid(1, steps, 100.0);
        CSplitterUnit splitter(2);
        splitter.SetFractions({ 0.3, 0.7 });
        splitter.Initialize(grid, 42);
        CStreamSlice input;
        input.Resize(300, 1, ERepresentation::Particles);
        AddCards(input, standard, 2000, 0, 1.0);
        const double cardMax = MaxCardMass(input);
        const double inputMean = MeanSize(input);
        std::vector<CStreamSlice> outputs(2);
        double worstStep = 0.0, worstTotal = 0.0, sumA = 0.0, sumIn = 0.0, worstMean = 0.0, worstAccounts = 0.0;
        bool countsOk = true;
        // je Groessenbereich (Siebsatz) Masse nach A und insgesamt
        const SCardSettings settings;
        std::vector<double> rangeA(40, 0.0), rangeIn(40, 0.0);
        for (size_t k = 0; k < steps; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            const double massA = outputs[0].GetCardsMassFlow();
            countsOk = countsOk && outputs[0].GetNumCards() + outputs[1].GetNumCards() == input.GetNumCards();
            worstStep = std::max(worstStep, std::abs(massA - 0.3 * input.GetCardsMassFlow()));
            sumA += massA;
            sumIn += input.GetCardsMassFlow();
            worstTotal = std::max(worstTotal, std::abs(sumA - 0.3 * sumIn));
            worstMean = std::max(worstMean, std::abs(MeanSize(outputs[0]) - inputMean) / inputMean);
            const std::vector<double>& accounts = splitter.GetCardAccounts();
            worstAccounts = std::max(worstAccounts, std::abs(accounts[0] + accounts[1]));
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                rangeA[GetCardRangeIndex(outputs[0].GetCardSize(c), settings) + 10] += outputs[0].GetCardMassFlow(c);
            for (size_t c = 0; c < input.GetNumCards(); ++c)
                rangeIn[GetCardRangeIndex(input.GetCardSize(c), settings) + 10] += input.GetCardMassFlow(c);
        }
        double worstRange = 0.0;
        for (size_t r = 0; r < rangeIn.size(); ++r)
            if (rangeIn[r] > 0.01 * sumIn)
                worstRange = std::max(worstRange, std::abs(rangeA[r] / rangeIn[r] - 0.3));
        std::printf("      groesste Karte %.2e kg/s; Abweichung je Schritt hoechstens %.2e kg/s, aufsummiert hoechstens %.2e kg/s\n",
            cardMax, worstStep, worstTotal);
        std::printf("      mittlere Groesse in A: hoechstens %.2f %% neben dem Eingang; Anteil nach A je Groessenbereich: hoechstens %.1e neben 30 %%\n",
            worstMean * 100.0, worstRange);
        Check(countsOk, "Splitter: Kartenzahl bleibt (A + B = Eingang, nichts kopiert)");
        Check(worstStep <= cardMax, "Splitter: je Schritt weicht A hoechstens um eine Karte vom Soll ab");
        Check(worstTotal <= cardMax, "Splitter: aufsummiert ueber 1000 Schritte ebenfalls hoechstens eine Karte (kein Wegdriften)");
        Check(worstAccounts < 1e-12, "Splitter: Summe der Konten bleibt 0");
        Check(worstMean < 0.01, "Splitter: mittlere Groesse in A wie im Eingang (1 %) in jedem Schritt");
        Check(worstRange < 2e-3, "Splitter: jeder Groessenbereich geht zu 30 % nach A");
    }

    // 3) Zwei Stoffe mit denselben Kartengroessen, 50 / 50: jeder Stoff wird
    //    fuer sich geteilt (nach reiner Groessenordnung ginge ein Stoff ganz nach A).
    {
        const SSimulationGrid grid = MakeGrid(2, 100, 10.0);
        CSplitterUnit splitter(2);
        splitter.SetFractions({ 0.5, 0.5 });
        splitter.Initialize(grid, 7);
        CStreamSlice input;
        input.Resize(300, 2, ERepresentation::Particles);
        AddCards(input, standard, 2000, 0, 1.0);
        AddCards(input, standard, 2000, 1, 1.0);
        std::vector<CStreamSlice> outputs(2);
        double worst = 0.0;
        for (size_t k = 0; k < 100; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 2, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            worst = std::max(worst, std::abs(outputs[0].GetCardsMassFlow(0) - 0.5));
            worst = std::max(worst, std::abs(outputs[0].GetCardsMassFlow(1) - 0.5));
        }
        std::printf("      zwei Stoffe, 50/50: Sand und Kohle in A hoechstens %.2e kg/s neben 0,5 kg/s\n", worst);
        Check(worst <= 2.0 * MaxCardMass(input), "Splitter: zwei Stoffe gleicher Groesse - jeder Stoff je Schritt zur Haelfte in A");
    }

    // 4) Drei Ausgaenge 50 / 30 / 20: aufsummiert jeder Ausgang auf eine Karte genau.
    {
        const SSimulationGrid grid = MakeGrid(1, 500, 50.0);
        CSplitterUnit splitter(3);
        splitter.SetFractions({ 0.5, 0.3, 0.2 });
        splitter.Initialize(grid, 3);
        CStreamSlice input;
        input.Resize(300, 1, ERepresentation::Particles);
        AddCards(input, standard, 2000, 0, 1.0);
        std::vector<CStreamSlice> outputs(3);
        std::vector<double> sums(3, 0.0);
        double sumIn = 0.0, worst = 0.0;
        for (size_t k = 0; k < 500; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            sumIn += input.GetCardsMassFlow();
            const double f[3] = { 0.5, 0.3, 0.2 };
            for (size_t i = 0; i < 3; ++i)
            {
                sums[i] += outputs[i].GetCardsMassFlow();
                worst = std::max(worst, std::abs(sums[i] - f[i] * sumIn));
            }
        }
        Check(worst <= MaxCardMass(input), "Splitter mit drei Ausgaengen: aufsummiert jeder Ausgang auf eine Karte genau");
    }

    // 5) Rueckfuehrung ohne Hold-up (Merger -> Splitter, 50 % zurueck): Die
    //    Kartenzahl im Kreis bleibt begrenzt (N / (1 - R)), der Austrag
    //    erreicht den Zulauf - ohne Zusammenlegen (Methoden-Dokument 13.19, Fall C).
    {
        for (double recycle : { 0.5, 0.9 })
        {
            const size_t steps = 200;
            const SSimulationGrid grid = MakeGrid(1, steps, 20.0);
            CMergerUnit merger(2);
            CSplitterUnit splitter(2);
            splitter.SetFractions({ 1.0 - recycle, recycle });
            merger.Initialize(grid, 1);
            splitter.Initialize(grid, 2);
            CStreamSlice inlet, recycled;
            inlet.Resize(300, 1, ERepresentation::Particles);
            recycled.Resize(300, 1, ERepresentation::Particles);
            AddCards(inlet, standard, 2000, 0, 1.0);
            std::vector<CStreamSlice> merged(1), split(2);
            size_t loopCards = 0;
            double outMass = 0.0;
            for (size_t k = 0; k < steps; ++k)
            {
                merged[0].Resize(300, 1, ERepresentation::Particles);
                merger.Step({ k, 0.1 * k, 0.1 }, { &inlet, &recycled }, merged);
                for (CStreamSlice& o : split)
                    o.Resize(300, 1, ERepresentation::Particles);
                splitter.Step({ k, 0.1 * k, 0.1 }, { &merged[0] }, split);
                recycled = split[1];
                loopCards = merged[0].GetNumCards();
                outMass = split[0].GetCardsMassFlow();
            }
            const double expected = 2000.0 / (1.0 - recycle);
            std::printf("      Rueckfuehrung R = %.1f: %zu Karten im Kreis (erwartet %.0f), Austrag %.6f kg/s bei 1 kg/s Zulauf\n",
                recycle, loopCards, expected, outMass);
            Check(std::abs(static_cast<double>(loopCards) - expected) < 0.02 * expected && NearAbs(outMass, 1.0, 0.01),
                std::string("Rueckfuehrung R = ") + (recycle == 0.5 ? "0,5" : "0,9") + ": Kartenzahl N / (1 - R), Austrag = Zulauf");
        }
    }

    // 6) Im Flowsheet: zwei Inlets -> Merger -> Splitter (OU) -> Outlets.
    //    Gleicher Seed: Der OU-Verlauf ist im Kontinuum und im Partikelmodus
    //    derselbe, die Massenstroeme liegen je Schritt hoechstens eine Karte
    //    auseinander; Masse je Stoff bleibt erhalten.
    {
        CFlowsheetGraph graph;
        const std::string inA = graph.AddInletNode("Inlet1", 0, 0);
        const std::string inB = graph.AddInletNode("Inlet2", 0, 1);
        const std::string mg = graph.AddMergerNode("Merger1", 2, 1, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 2, 0);
        const std::string o1 = graph.AddOutletNode("Outlet1", 3, 0);
        const std::string o2 = graph.AddOutletNode("Outlet2", 3, 1);
        graph.GetInlet(inA)->SetTimeProfile({ { 0.0, 2.0 }, { 100.0, 2.0 } });
        graph.GetInlet(inB)->SetTimeProfile({ { 0.0, 1.0 }, { 100.0, 1.0 } });
        graph.GetInlet(inB)->SetMaterial("Coal");
        graph.GetSplitter(sp)->SetStochastic(true);
        graph.GetSplitter(sp)->SetFractions({ 0.4, 0.6 });
        graph.GetSplitter(sp)->SetFluctuations({ { 0.05, 1.0 } });
        graph.AddConnection(inA, 0, mg, 0);
        graph.AddConnection(inB, 0, mg, 1);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, o1, 0);
        graph.AddConnection(sp, 1, o2, 0);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 201, 20.0);
        graph.SetRandomSeed(11);
        graph.RunSimulations(1);
        const CSimulationResults continuum = graph.GetDisplayRecord(o1, 0)->GetTotal();
        graph.SetRepresentation(ERepresentation::Particles);
        Check(graph.RunSimulations(1) == 1, "Flowsheet Inlets -> Merger -> Splitter -> Outlets laeuft im Partikelmodus");
        const CStreamRecord& cards1 = *graph.GetDisplayRecord(o1, 0);
        const CStreamRecord& cards2 = *graph.GetDisplayRecord(o2, 0);
        const double cardMax = 2.0 / 2000.0 * 1.5; // groesste Karte: hoechstens 1,5 Mittelwerte (Mindestzahl-Bereiche sind leichter)
        // Ohne Materialdatei stehen die Stoffe alphabetisch: Coal vor Sand.
        const std::vector<std::string>& names = cards1.GetCompoundNames();
        const size_t iSand = static_cast<size_t>(std::find(names.begin(), names.end(), "Sand") - names.begin());
        const size_t iCoal = static_cast<size_t>(std::find(names.begin(), names.end(), "Coal") - names.begin());
        double worstOu = 0.0, worstBalance = 0.0;
        for (size_t k = 0; k < 201; ++k)
        {
            worstOu = std::max(worstOu, std::abs(cards1.GetTotal().IntegrateOverSize(k) - continuum.IntegrateOverSize(k)));
            const double sand = cards1.GetCompound(iSand).IntegrateOverSize(k) + cards2.GetCompound(iSand).IntegrateOverSize(k);
            const double coal = cards1.GetCompound(iCoal).IntegrateOverSize(k) + cards2.GetCompound(iCoal).IntegrateOverSize(k);
            worstBalance = std::max(worstBalance, std::max(std::abs(sand - 2.0), std::abs(coal - 1.0)));
        }
        std::printf("      OU-Splitter: Outlet 1 Karten gegen Kontinuum hoechstens %.2e kg/s auseinander; Bilanz je Stoff %.1e kg/s\n",
            worstOu, worstBalance);
        Check(worstOu <= cardMax, "OU-Splitter: gleicher Verlauf wie im Kontinuum (je Schritt hoechstens eine Karte)");
        Check(worstBalance < 1e-12, "Flowsheet: Sand und Kohle kommen je Schritt vollstaendig an den Outlets an");
    }

    // 7) Dosierwaage (Methoden-Dokument Kap. 15): Inlet 100 kg/h -> OU-Splitter
    //    (mu 0,6944, sigma 0,0081, tau 0,3 s) -> Auffangwaage, 1800 s, dt 0,1 s.
    //    Auswertung wie im Fenster "Auswertung" (1-s-Mittel): Kontinuum und
    //    Partikelmodus liefern dieselbe Statistik.
    {
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 1, 0);
        const std::string scale = graph.AddOutletNode("Auffangwaage", 2, 0);
        const std::string rest = graph.AddOutletNode("Rest", 2, 1);
        graph.GetInlet(in)->SetTimeProfile({ { 0.0, 100.0 / 3600.0 }, { 2000.0, 100.0 / 3600.0 } });
        graph.GetSplitter(sp)->SetStochastic(true);
        graph.GetSplitter(sp)->SetFractions({ 0.6944, 1.0 - 0.6944 });
        graph.GetSplitter(sp)->SetFluctuations({ { 0.0081, 0.3 } });
        graph.AddConnection(in, 0, sp, 0);
        graph.AddConnection(sp, 0, scale, 0);
        graph.AddConnection(sp, 1, rest, 0);
        graph.ConfigureGrid(50, 1e-6, 1e-3, 18001, 1800.0);
        graph.SetRandomSeed(2012);
        SSignalStatistics stats[2];
        for (int mode = 0; mode < 2; ++mode)
        {
            graph.SetRepresentation(mode == 0 ? ERepresentation::Continuum : ERepresentation::Particles);
            graph.RunSimulations(1);
            const std::vector<double>& run = graph.GetStreamStatistics(scale, 0)->GetTotal().GetRunTimeSeries()[0];
            std::vector<double> kgPerHour(run.size());
            for (size_t k = 0; k < run.size(); ++k)
                kgPerHour[k] = run[k] * 3600.0;
            stats[mode] = ComputeSignalStatistics(AverageOverWindows(kgPerHour, 0, StepsPerWindow(0.1, 1.0)));
        }
        std::printf("      Dosierwaage 1-s-Mittel: Kontinuum %.4f +- %.4f kg/h, Partikel %.4f +- %.4f kg/h\n",
            stats[0].mean, stats[0].stdDev, stats[1].mean, stats[1].stdDev);
        Check(NearRel(stats[1].mean, stats[0].mean, 1e-4) && NearRel(stats[1].stdDev, stats[0].stdDev, 0.01),
            "Dosierwaage: Mittel (1e-4) und Streuung (1 %) im Partikelmodus wie im Kontinuum");
    }

    return Summary();
}
