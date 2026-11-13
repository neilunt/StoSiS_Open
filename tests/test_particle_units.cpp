// Merger, Splitter und Verzoegerung im Partikelmodus (Methodenhandbuch 7.3, 9.4, 9.7):
// Karten aneinanderhaengen, Konto-Verfahren, Vergleich mit dem Kontinuum.
#include "FlowsheetGraph.h"
#include "CardPlacement.h"
#include "CardDistribution.h"
#include "CardLimits.h"
#include "SignalAnalysis.h"
#include "SizeDependentDelayProcessor.h"
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
    const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6);
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
        m = std::max(m, slice.GetCardMass(k));
    return m;
}

// Massengewichtete mittlere Groesse der Karten einer Scheibe.
static double MeanSize(const CStreamSlice& slice)
{
    double mass = 0.0, moment = 0.0;
    for (size_t k = 0; k < slice.GetNumCards(); ++k)
    {
        mass += slice.GetCardMass(k);
        moment += slice.GetCardMass(k) * slice.GetCardSize(k);
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
        Check(NearRel(out.GetCardsMass(0), 3.0, 1e-12) && NearRel(out.GetCardsMass(1), 1.0, 1e-12),
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
            const double massA = outputs[0].GetCardsMass();
            countsOk = countsOk && outputs[0].GetNumCards() + outputs[1].GetNumCards() == input.GetNumCards();
            worstStep = std::max(worstStep, std::abs(massA - 0.3 * input.GetCardsMass()));
            sumA += massA;
            sumIn += input.GetCardsMass();
            worstTotal = std::max(worstTotal, std::abs(sumA - 0.3 * sumIn));
            worstMean = std::max(worstMean, std::abs(MeanSize(outputs[0]) - inputMean) / inputMean);
            const std::vector<double>& accounts = splitter.GetCardAccounts();
            worstAccounts = std::max(worstAccounts, std::abs(accounts[0] + accounts[1]));
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                rangeA[GetCardRangeIndex(outputs[0].GetCardSize(c), settings) + 10] += outputs[0].GetCardMass(c);
            for (size_t c = 0; c < input.GetNumCards(); ++c)
                rangeIn[GetCardRangeIndex(input.GetCardSize(c), settings) + 10] += input.GetCardMass(c);
        }
        double worstRange = 0.0;
        for (size_t r = 0; r < rangeIn.size(); ++r)
            if (rangeIn[r] > 0.01 * sumIn)
                worstRange = std::max(worstRange, std::abs(rangeA[r] / rangeIn[r] - 0.3));
        std::printf("      groesste Karte %.2e kg; Abweichung je Schritt hoechstens %.2e kg, aufsummiert hoechstens %.2e kg\n",
            cardMax, worstStep, worstTotal);
        std::printf("      mittlere Groesse in A: hoechstens %.2f %% neben dem Eingang; Anteil nach A je Groessenbereich: hoechstens %.1e neben 30 %%\n",
            worstMean * 100.0, worstRange);
        Check(countsOk, "Splitter: Kartenzahl bleibt (A + B = Eingang, nichts kopiert)");
        // Das Konto zaehlt Kartenanteile, nicht Masse: je Schritt etwa eine
        // Karte daneben (Schranke zwei), und die Abweichungen der Masse
        // gleichen sich nicht exakt aus, bleiben aber winzig.
        Check(worstStep <= 2.0 * cardMax, "Splitter: je Schritt weicht A hoechstens um zwei Karten vom Soll ab");
        Check(worstTotal <= 1e-4 * 0.3 * sumIn, "Splitter: aufsummiert ueber 1000 Schritte weniger als 1e-4 neben dem Soll");
        Check(worstAccounts < 1e-12, "Splitter: Summe der Konten bleibt 0");
        Check(worstMean < 0.01, "Splitter: mittlere Groesse in A wie im Eingang (1 %) in jedem Schritt");
        Check(worstRange < 2e-3, "Splitter: jeder Groessenbereich geht zu 30 % nach A");
    }

    // 2b) Sehr ungleich schwere Karten (Log-Normal sigma_ln = 0,6, Summe
    //     f = 0,5, gamma = 1: die feinsten Karten tragen etwa 1/1000 der
    //     mittleren Masse), Splitter 70 / 30. Die feinsten 5 % der Partikel
    //     gehen nach Anzahl und Masse zu 70 % nach A und fehlen in keinem
    //     Schritt in einem Ausgang. Mit einem Konto in Masse gingen sie
    //     geschlossen in einen Ausgang und auf Dauer nur zu etwa 54 % nach A.
    {
        const size_t steps = 2000;
        SSimulationGrid grid = MakeGrid(1, steps, 200.0);
        grid.cards.numberShare = 0.5;
        grid.cards.exponent = 1.0;
        CSplitterUnit splitter(2);
        splitter.SetFractions({ 0.7, 0.3 });
        splitter.Initialize(grid, 5);
        const SCardPlacement placement = PlaceCardsAtQuantiles({ EDistributionType::LogNormal, 100e-6, 0.6 }, grid.cards,
            grid.GetCardCountMin());
        CStreamSlice input;
        input.Resize(300, 1, ERepresentation::Particles);
        for (size_t k = 0; k < placement.sizes.size(); ++k)
            input.AddCard(placement.sizes[k], 0, placement.massFractions[k]);
        const double fineSize = placement.fineSize;
        double fineMassIn = 0.0, lightest = 1.0;
        for (size_t c = 0; c < input.GetNumCards(); ++c)
        {
            fineMassIn += input.GetCardSize(c) < fineSize ? input.GetCardMass(c) : 0.0;
            lightest = std::min(lightest, input.GetCardMass(c) * static_cast<double>(input.GetNumCards()));
        }
        std::vector<CStreamSlice> outputs(2);
        double fineCardsA = 0.0, fineCardsAll = 0.0, fineMassA = 0.0;
        size_t emptySteps = 0;
        for (size_t k = 0; k < steps; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            size_t inA = 0, inB = 0;
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                if (outputs[0].GetCardSize(c) < fineSize)
                {
                    ++inA;
                    fineMassA += outputs[0].GetCardMass(c);
                }
            for (size_t c = 0; c < outputs[1].GetNumCards(); ++c)
                inB += outputs[1].GetCardSize(c) < fineSize ? 1 : 0;
            emptySteps += inA == 0 || inB == 0 ? 1 : 0;
            fineCardsA += static_cast<double>(inA);
            fineCardsAll += static_cast<double>(inA + inB);
        }
        const double shareCards = fineCardsA / fineCardsAll, shareMass = fineMassA / (fineMassIn * static_cast<double>(steps));
        std::printf("      ungleich schwere Karten (leichteste %.1e x Mittel): feinste 5 %% der Partikel nach A %.2f %% der "
            "Karten, %.2f %% der Masse; Schritte mit leerem Ausgang %zu\n", lightest, 100.0 * shareCards, 100.0 * shareMass,
            emptySteps);
        Check(NearAbs(shareCards, 0.7, 0.01) && NearAbs(shareMass, 0.7, 0.02) && emptySteps == 0,
            "Splitter: leichte Karten zu 70 % nach A, in jedem Schritt in beiden Ausgaengen (Konto in Kartenanteilen)");
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
            worst = std::max(worst, std::abs(outputs[0].GetCardsMass(0) - 0.5));
            worst = std::max(worst, std::abs(outputs[0].GetCardsMass(1) - 0.5));
        }
        std::printf("      zwei Stoffe, 50/50: Sand und Kohle in A hoechstens %.2e kg neben 0,5 kg\n", worst);
        Check(worst <= 2.0 * MaxCardMass(input), "Splitter: zwei Stoffe gleicher Groesse - jeder Stoff je Schritt zur Haelfte in A");
    }

    // 4) Drei Ausgaenge 50 / 30 / 20: je Schritt jeder Ausgang auf etwa eine
    //    Karte genau, aufsummiert auf weniger als 1e-4.
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
        double sumIn = 0.0, worst = 0.0, worstStep = 0.0;
        for (size_t k = 0; k < 500; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            sumIn += input.GetCardsMass();
            const double f[3] = { 0.5, 0.3, 0.2 };
            for (size_t i = 0; i < 3; ++i)
            {
                worstStep = std::max(worstStep, std::abs(outputs[i].GetCardsMass() - f[i] * input.GetCardsMass()));
                sums[i] += outputs[i].GetCardsMass();
                worst = std::max(worst, std::abs(sums[i] - f[i] * sumIn));
            }
        }
        std::printf("      drei Ausgaenge: je Schritt hoechstens %.2f Karten, aufsummiert %.1e des Durchsatzes neben dem Soll\n",
            worstStep / MaxCardMass(input), worst / sumIn);
        Check(worstStep <= 1.5 * MaxCardMass(input) && worst <= 1e-4 * sumIn,
            "Splitter mit drei Ausgaengen: je Schritt etwa eine Karte, aufsummiert weniger als 1e-4 neben dem Soll");
    }

    // 5) Rueckfuehrung ohne Hold-up (Merger -> Splitter, 50 % zurueck): Die
    //    Kartenzahl im Kreis bleibt begrenzt (N / (1 - R)), der Austrag
    //    erreicht den Zulauf - ohne Zusammenlegen (Methodenhandbuch 9.5).
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
                outMass = split[0].GetCardsMass();
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
        const double cardMax = 2.0 / 2000.0 * 1.5; // groesste Karte: hoechstens 1,5 Mittelwerte (Kartendichte Standard: 1,4)
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

    // 7) Dosierwaage (Methodenhandbuch Kap. 11): Inlet 100 kg/h -> OU-Splitter
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

    // 6) Grenzen je Stromscheibe (Methodenhandbuch 9.5): ueber 2 N
    //    Karten zusammenlegen (leichteste Paare nach Anteil an der
    //    Kartendichte), Karten mit mehr als 5 / N teilen; die Masse bleibt
    //    genau erhalten, beim Zusammenlegen auch die Partikelzahl.
    {
        const SCardSettings settings; // N = 2000, Summe f = 0,5, gamma = 0,8, hoechstens 2 N Karten und 5 / N je Karte
        const double countMin = 1e-6;
        const auto sizesOf = [](const CStreamSlice& s)
        {
            std::vector<double> v(s.GetNumCards());
            for (size_t k = 0; k < v.size(); ++k)
                v[k] = s.GetCardSize(k);
            return v;
        };
        const auto massesOf = [](const CStreamSlice& s)
        {
            std::vector<double> v(s.GetNumCards());
            for (size_t k = 0; k < v.size(); ++k)
                v[k] = s.GetCardMass(k);
            return v;
        };
        const auto numberOf = [](const CStreamSlice& s)
        {
            double n = 0.0;
            for (size_t k = 0; k < s.GetNumCards(); ++k)
                n += s.GetCardMass(k) / std::pow(s.GetCardSize(k), 3.0);
            return n;
        };
        // Innerhalb der Grenzen: hoechstens 2 N Karten, kein Anteil ueber 5 / N.
        const double heaviest = settings.splitFactor / static_cast<double>(settings.cardsPerSlice);
        const auto withinLimits = [&](const CStreamSlice& s)
        {
            bool ok = s.GetNumCards() <= static_cast<size_t>(settings.upperFactor * static_cast<double>(settings.cardsPerSlice));
            for (double share : ComputeCardShares(sizesOf(s), massesOf(s), settings, countMin))
                ok = ok && share <= heaviest * (1.0 + 1e-9);
            return ok;
        };
        // Masse je Groesse: Teilen darf nur Stuecke gleicher Groesse erzeugen.
        const auto massBySize = [](const CStreamSlice& s)
        {
            std::vector<std::pair<double, double>> v;
            for (size_t k = 0; k < s.GetNumCards(); ++k)
                v.push_back({ s.GetCardSize(k), s.GetCardMass(k) });
            std::sort(v.begin(), v.end());
            std::vector<std::pair<double, double>> merged;
            for (const auto& entry : v)
                if (!merged.empty() && merged.back().first == entry.first)
                    merged.back().second += entry.second;
                else
                    merged.push_back(entry);
            return merged;
        };

        // a) Karten eines Inlets tragen je etwa 1/N: nichts zu tun.
        CStreamSlice inlet;
        inlet.Resize(300, 1, ERepresentation::Particles);
        AddCards(inlet, standard, 2000, 0, 1.0);
        const std::vector<double> inletShares = ComputeCardShares(sizesOf(inlet), massesOf(inlet), settings, countMin);
        const auto [lightest, heaviestInlet] = std::minmax_element(inletShares.begin(), inletShares.end());
        std::printf("      Inlet: Anteil je Karte %.3f bis %.3f / N\n", *lightest * 2000.0, *heaviestInlet * 2000.0);
        CStreamSlice same = inlet;
        const SLimitCounts none = ApplySliceLimits(same, settings, countMin);
        bool identical = same.GetNumCards() == inlet.GetNumCards();
        for (size_t k = 0; identical && k < inlet.GetNumCards(); ++k)
            identical = same.GetCardSize(k) == inlet.GetCardSize(k) && same.GetCardMass(k, 0) == inlet.GetCardMass(k, 0);
        Check(identical && none.added == 0 && none.removed == 0, "Grenzen: Inlet-Karten bleiben unveraendert");
        Check(*lightest * 2000.0 > 0.5 && *heaviestInlet * 2000.0 < 2.0, "Inlet: jede Karte traegt zwischen 1/(2N) und 2/N");

        // b) Teilen: 20 Karten (jede 100. des Inlets, wie hinter einem 1-%-Splitterausgang).
        CStreamSlice few;
        few.Resize(300, 1, ERepresentation::Particles);
        for (size_t k = 50; k < inlet.GetNumCards(); k += 100)
            few.AddCard(inlet.GetCardSize(k), 0, inlet.GetCardMass(k, 0));
        CStreamSlice split = few;
        const SLimitCounts added = ApplySliceLimits(split, settings, countMin);
        std::printf("      Teilen: %zu Karten -> %zu (je Karte hoechstens 5 / N)\n", few.GetNumCards(), split.GetNumCards());
        Check(withinLimits(split) && added.added == split.GetNumCards() - few.GetNumCards() && added.removed == 0,
            "Teilen: keine Karte traegt danach mehr als 5 / N");
        Check(split.GetNumCards() >= 380 && split.GetNumCards() <= 440,
            "Teilen: 20 Karten mit je etwa 1/20 -> zusammen etwa N / 5 = 400 Stuecke");
        const auto beforeSizes = massBySize(few), afterSizes = massBySize(split);
        bool sameMassBySize = beforeSizes.size() == afterSizes.size();
        for (size_t i = 0; sameMassBySize && i < beforeSizes.size(); ++i)
            sameMassBySize = beforeSizes[i].first == afterSizes[i].first
                && NearRel(afterSizes[i].second, beforeSizes[i].second, 1e-12);
        Check(sameMassBySize && NearRel(split.GetCardsMass(), few.GetCardsMass(), 1e-14),
            "Teilen: nur Stuecke gleicher Groesse, Masse je Groesse genau erhalten (Raster unveraendert)");
        // Stueckzahl je Karte nach ihrem Anteil: nur nach Masse (f = 0, ohne
        // Abflachung), N = 100, also hoechstens 0,05 je Karte.
        {
            SCardSettings massOnly = settings;
            massOnly.cardsPerSlice = 100;
            massOnly.numberShare = 0.0;
            massOnly.exponent = 1.0;
            CStreamSlice two;
            two.Resize(300, 1, ERepresentation::Particles);
            two.AddCard(100e-6, 0, 0.9);
            two.AddCard(200e-6, 0, 0.1);
            ApplySliceLimits(two, massOnly, countMin);
            size_t at100 = 0, at200 = 0;
            for (size_t k = 0; k < two.GetNumCards(); ++k)
            {
                at100 += two.GetCardSize(k) == 100e-6 ? 1 : 0;
                at200 += two.GetCardSize(k) == 200e-6 ? 1 : 0;
            }
            Check(at100 == 18 && at200 == 2 && NearRel(two.GetCardsMass(), 1.0, 1e-14),
                "Teilen je Karte nach ihrem Anteil: 0,9 -> 18 Stuecke, 0,1 -> 2 Stuecke (je hoechstens 5 / N)");
        }

        // c) Zusammenlegen: 10 Scheiben aus zwei verschiedenen Verteilungen in einer.
        const SSizeDistribution coarse{ EDistributionType::LogNormal, 120e-6, 0.3 };
        CStreamSlice many;
        many.Resize(300, 1, ERepresentation::Particles);
        for (int i = 0; i < 10; ++i)
            AddCards(many, i % 2 == 0 ? standard : coarse, 2000, 0, 0.1);
        CStreamSlice merged = many;
        const SLimitCounts removed = ApplySliceLimits(merged, settings, countMin);
        const SCardCumulative q0 = MakeCardCumulative(sizesOf(many), massesOf(many));
        const SCardCumulative q1 = MakeCardCumulative(sizesOf(merged), massesOf(merged));
        double worstQuantile = 0.0;
        for (double f : { 0.1, 0.5, 0.9 })
            worstQuantile = std::max(worstQuantile, std::abs(GetCardQuantile(q1, f) / GetCardQuantile(q0, f) - 1.0));
        // Anteil der Karten in den groebsten 2 % der Masse vor und nach dem Zusammenlegen.
        const double coarseEdge = GetCardQuantile(q0, 0.98);
        const auto coarseShare = [&](const CStreamSlice& s)
        {
            size_t n = 0;
            for (size_t k = 0; k < s.GetNumCards(); ++k)
                n += s.GetCardSize(k) > coarseEdge ? 1 : 0;
            return static_cast<double>(n) / static_cast<double>(s.GetNumCards());
        };
        std::printf("      Zusammenlegen: %zu Karten -> %zu, d10/d50/d90 um hoechstens %.3f %% verschoben, "
            "Karten in den groebsten 2 %% der Masse: %.2f %% -> %.2f %%\n", many.GetNumCards(), merged.GetNumCards(),
            100.0 * worstQuantile, 100.0 * coarseShare(many), 100.0 * coarseShare(merged));
        Check(withinLimits(merged) && removed.removed == many.GetNumCards() - merged.GetNumCards() && removed.added == 0,
            "Zusammenlegen: hoechstens 2 N Karten");
        Check(coarseShare(merged) >= 0.7 * coarseShare(many),
            "Zusammenlegen: die Randkarten bleiben anteilig erhalten (nach Anteil an der Kartendichte, nicht nach Masse)");
        Check(NearRel(merged.GetCardsMass(), many.GetCardsMass(), 1e-12) && NearRel(numberOf(merged), numberOf(many), 1e-10),
            "Zusammenlegen: Masse und Partikelzahl genau erhalten");
        Check(worstQuantile < 0.005, "Zusammenlegen: d10, d50, d90 verschieben sich um weniger als 0,5 %");

        // d) Zwei Stoffe: zusammengelegt wird nur innerhalb eines Stoffs.
        CStreamSlice mixed;
        mixed.Resize(300, 2, ERepresentation::Particles);
        for (int i = 0; i < 4; ++i)
        {
            AddCards(mixed, standard, 2000, 0, 0.3);
            AddCards(mixed, standard, 2000, 1, 0.2);
        }
        CStreamSlice mixedMerged = mixed;
        ApplySliceLimits(mixedMerged, settings, countMin);
        bool pure = true;
        for (size_t k = 0; k < mixedMerged.GetNumCards(); ++k)
            pure = pure && (mixedMerged.GetCardMass(k, 0) == 0.0 || mixedMerged.GetCardMass(k, 1) == 0.0);
        Check(pure && mixedMerged.GetNumCards() < mixed.GetNumCards()
            && NearRel(mixedMerged.GetCardsMass(0), mixed.GetCardsMass(0), 1e-12)
            && NearRel(mixedMerged.GetCardsMass(1), mixed.GetCardsMass(1), 1e-12),
            "Zwei Stoffe: jede Karte bleibt aus einem Stoff, Masse je Stoff genau erhalten");

        // e) Rueckfuehrung R = 0,9 wie in 5), jetzt mit Grenzen nach Merger und Splitter.
        const size_t steps = 200;
        const SSimulationGrid grid = MakeGrid(1, steps, 20.0);
        CMergerUnit merger(2);
        CSplitterUnit splitter(2);
        splitter.SetFractions({ 0.1, 0.9 });
        merger.Initialize(grid, 1);
        splitter.Initialize(grid, 2);
        CStreamSlice recycled;
        recycled.Resize(300, 1, ERepresentation::Particles);
        std::vector<CStreamSlice> loop(1), outputs(2);
        size_t loopCards = 0, maxLoopCards = 0;
        double outMass = 0.0;
        for (size_t k = 0; k < steps; ++k)
        {
            loop[0].Resize(300, 1, ERepresentation::Particles);
            merger.Step({ k, 0.1 * k, 0.1 }, { &inlet, &recycled }, loop);
            ApplySliceLimits(loop[0], settings, countMin);
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &loop[0] }, outputs);
            for (CStreamSlice& o : outputs)
                ApplySliceLimits(o, settings, countMin);
            recycled = outputs[1];
            loopCards = loop[0].GetNumCards();
            maxLoopCards = std::max(maxLoopCards, loopCards);
            outMass = outputs[0].GetCardsMass();
        }
        std::printf("      Rueckfuehrung R = 0,9 mit Grenzen: %zu Karten im Kreis (hoechstens %zu; ohne Grenzen 20000), "
            "Austrag %.6f kg/s\n", loopCards, maxLoopCards, outMass);
        Check(maxLoopCards <= 2 * settings.cardsPerSlice + 10 && NearAbs(outMass, 1.0, 0.01),
            "Rueckfuehrung R = 0,9: Grenzen deckeln die Karten im Kreis bei etwa 2 N, Austrag = Zulauf");

        // f) Im Flowsheet: Inlet -> OU-Splitter 10/90 -> 2 Outlets (am 10-%-
        //    Ausgang traegt jede Karte etwa 10 / N und wird geteilt). Teilen
        //    aendert die Ergebnisse auf dem Raster nicht.
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 1, 0);
        const std::string a = graph.AddOutletNode("Outlet1", 2, 0);
        const std::string b = graph.AddOutletNode("Outlet2", 2, 1);
        graph.GetSplitter(sp)->SetStochastic(true);
        graph.GetSplitter(sp)->SetFractions({ 0.1, 0.9 });
        graph.AddConnection(in, 0, sp, 0);
        graph.AddConnection(sp, 0, a, 0);
        graph.AddConnection(sp, 1, b, 0);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 101, 20.0);
        graph.SetRepresentation(ERepresentation::Particles);
        SCardSettings off = graph.GetCardSettings();
        off.sliceLimits = false;
        graph.SetCardSettings(off);
        graph.RunSimulations(1);
        const CSimulationResults without = *graph.GetDisplayResults(a, 0);
        SCardSettings on = off;
        on.sliceLimits = true;
        graph.SetCardSettings(on);
        graph.RunSimulations(1);
        const CSimulationResults with = *graph.GetDisplayResults(a, 0);
        double worstDiff = 0.0, largest = 0.0;
        for (size_t t = 0; t < with.GetNumTimeSteps(); ++t)
            for (size_t s = 0; s < with.GetNumSizeClasses(); ++s)
            {
                worstDiff = std::max(worstDiff, std::abs(with.GetValue(t, s) - without.GetValue(t, s)));
                largest = std::max(largest, std::abs(without.GetValue(t, s)));
            }
        bool allWithin = true;
        const CCardHistory* history = graph.GetCardHistory(sp, 0);
        const size_t numC = history->GetNumCompounds();
        for (const CCardHistory::SSnapshot& snapshot : history->GetSnapshots())
        {
            std::vector<double> masses(snapshot.sizes.size(), 0.0);
            for (size_t k = 0; k < masses.size(); ++k)
                for (size_t c = 0; c < numC; ++c)
                    masses[k] += snapshot.masses[k * numC + c];
            allWithin = allWithin && snapshot.sizes.size() <= 2 * on.cardsPerSlice;
            for (double share : ComputeCardShares(snapshot.sizes, masses, on, countMin))
                allWithin = allWithin && share <= heaviest * (1.0 + 1e-9);
        }
        const SUnitTiming& splitTiming = graph.GetSimulationTiming().units[1];
        std::printf("      Flowsheet: Splitter je Schritt +%.1f Karten durch Teilen, -%.1f durch Zusammenlegen; "
            "Ergebnis Outlet1 weicht um %.2e (relativ) ab\n", splitTiming.GetAddedPerStep(), splitTiming.GetRemovedPerStep(),
            largest > 0.0 ? worstDiff / largest : 0.0);
        Check(allWithin && splitTiming.cardsAdded > 0.0, "Flowsheet: jeder Splitterausgang in jedem Schritt innerhalb der Grenzen");
        Check(largest > 0.0 && worstDiff <= 1e-12 * largest, "Flowsheet: Teilen aendert das Ergebnis am Outlet nicht (1e-12)");
    }

    // 8) Size-dependent delay in particle mode (methods handbook 7.3, 9.4):
    //    every whole card leaves "early" after q steps or "late" after q + 1
    //    (D/dt = q + r), chosen by the account method; nothing is split.
    {
        // a) Same delay for all cards (p = 0), D = 2.3 dt, the same 2000 cards
        //    in every step: of each inflow step 70 % leave after 2 steps and
        //    30 % after 3. Mass and number of cards are kept, T is carried.
        const size_t steps = 101;
        const SSimulationGrid grid = MakeGrid(1, steps, 20.0);
        const double dt = grid.GetDt();
        CSizeDependentDelayProcessor delay;
        delay.SetParameter("delayRef", 2.3 * dt);
        delay.SetParameter("sizeExponent", 0.0);
        delay.Initialize(grid, 7);
        CStreamSlice input;
        input.Resize(300, 1, ERepresentation::Particles);
        AddCards(input, standard, 2000, 0, 1.0);
        input.SetTemperature(350.0);
        const double massIn = input.GetCardsMass();
        const double cardMax = MaxCardMass(input);
        std::vector<CStreamSlice> outputs(1);
        double sumIn = 0.0, sumOut = 0.0, worstFirst = 0.0, worstSteady = 0.0, worstAccount = 0.0, worstT = 0.0;
        size_t worstCount = 0;
        bool earlyEmpty = true;
        for (size_t k = 0; k < steps; ++k)
        {
            outputs[0].Resize(300, 1, ERepresentation::Particles);
            delay.Step({ k, dt * static_cast<double>(k), dt }, { &input }, outputs);
            const CStreamSlice& out = outputs[0];
            sumIn += massIn; // cards carry the mass of one step
            sumOut += out.GetCardsMass();
            if (k < 2)
                earlyEmpty = earlyEmpty && out.GetNumCards() == 0;
            else if (k == 2)
                worstFirst = std::abs(out.GetCardsMass() - 0.7 * massIn);
            else
            {
                worstSteady = std::max(worstSteady, std::abs(out.GetCardsMass() - massIn));
                const size_t count = out.GetNumCards();
                worstCount = std::max(worstCount, count > 2000 ? count - 2000 : 2000 - count);
            }
            if (out.GetNumCards() > 0)
                worstT = std::max(worstT, std::abs(out.GetTemperature() - 350.0));
            const double account = delay.GetLateAccount();
            worstAccount = std::max(worstAccount, account > 0.5 ? account - 0.5 : (account <= -0.5 ? -0.5 - account : 0.0));
        }
        const double balance = std::abs(sumIn - sumOut - delay.GetContentMass()) / sumIn;
        std::printf("      Verzoegerung 2,3 dt: Schritt 2 traegt %.4f statt 0,7 des Zulaufs; danach hoechstens %.2e kg "
            "(groesste Karte %.2e) und %zu Karten neben dem Zulauf; Bilanz %.1e\n",
            (0.7 * massIn + worstFirst) / massIn, worstSteady, cardMax, worstCount, balance);
        Check(earlyEmpty, "Verzoegerung: in den ersten zwei Schritten kommt nichts an");
        Check(worstFirst <= 3.0 * cardMax, "Verzoegerung: nach 2 Schritten 70 % des Zulaufs (hoechstens drei Karten daneben)");
        Check(worstSteady <= 3.0 * cardMax && worstCount <= 2, "Verzoegerung: danach je Schritt der ganze Zulauf (drei Karten), Kartenzahl bleibt (+-2)");
        Check(balance < 1e-12, "Verzoegerung: Zulauf = Ablauf + Inhalt (1e-12)");
        Check(worstAccount == 0.0, "Verzoegerung: Konto \"spaet\" bleibt zwischen -1/2 und 1/2");
        Check(worstT < 1e-9, "Verzoegerung: Temperatur wandert mit den Karten");
    }
    {
        // b) Size-dependent delay (p = 1, 2 s at 100 um, dt = 0.2 s), one pulse
        //    of 2000 cards in step 0: every card arrives after q or q + 1
        //    steps; mass-weighted mean arrival = mass-weighted mean D/dt; in
        //    every fifth of the sizes the number of late cards equals the sum
        //    of their remainders r within two cards.
        const size_t steps = 101;
        const SSimulationGrid grid = MakeGrid(1, steps, 20.0);
        const double dt = grid.GetDt();
        CSizeDependentDelayProcessor delay;
        delay.Initialize(grid, 3);
        const auto stepsOf = [&delay, dt](double size)
        {
            return delay.GetParameterValue("delayRef") * std::pow(size / delay.GetParameterValue("referenceSize"),
                delay.GetParameterValue("sizeExponent")) / dt;
        };
        CStreamSlice pulse, empty;
        pulse.Resize(300, 1, ERepresentation::Particles);
        empty.Resize(300, 1, ERepresentation::Particles);
        AddCards(pulse, standard, 2000, 0, 1.0);
        double massIn = 0.0, expected = 0.0;
        for (size_t c = 0; c < pulse.GetNumCards(); ++c)
        {
            massIn += pulse.GetCardMass(c);
            expected += pulse.GetCardMass(c) * stepsOf(pulse.GetCardSize(c));
        }
        expected /= massIn;
        // Per card (sorted by size): remainder r and whether it arrived late.
        std::vector<std::pair<double, int>> lateBySize; // size, late (1) or early (0)
        std::vector<double> remainders;
        std::vector<CStreamSlice> outputs(1);
        double massOut = 0.0, moment = 0.0;
        size_t cardsOut = 0;
        bool onTime = true;
        for (size_t k = 0; k < steps; ++k)
        {
            outputs[0].Resize(300, 1, ERepresentation::Particles);
            delay.Step({ k, dt * static_cast<double>(k), dt }, { k == 0 ? &pulse : &empty }, outputs);
            const CStreamSlice& out = outputs[0];
            for (size_t c = 0; c < out.GetNumCards(); ++c)
            {
                const double q = std::floor(stepsOf(out.GetCardSize(c)));
                const bool late = static_cast<double>(k) > q;
                onTime = onTime && (static_cast<double>(k) == q || static_cast<double>(k) == q + 1.0);
                lateBySize.emplace_back(out.GetCardSize(c), late ? 1 : 0);
                massOut += out.GetCardMass(c);
                moment += out.GetCardMass(c) * static_cast<double>(k);
                ++cardsOut;
            }
        }
        std::sort(lateBySize.begin(), lateBySize.end());
        double worstBand = 0.0;
        const size_t bands = 5, perBand = lateBySize.size() / bands;
        for (size_t b = 0; b < bands; ++b)
        {
            double late = 0.0, sumR = 0.0;
            for (size_t i = b * perBand; i < (b + 1) * perBand; ++i)
            {
                const double s = stepsOf(lateBySize[i].first);
                sumR += s - std::floor(s);
                late += lateBySize[i].second;
            }
            worstBand = std::max(worstBand, std::abs(late - sumR));
        }
        const double mean = moment / massOut;
        std::printf("      Puls, Verzoegerung je Groesse: mittlere Ankunft %.4f Schritte, Erwartung %.4f; "
            "spaete Karten je Fuenftel hoechstens %.2f neben der Summe der Reste\n", mean, expected, worstBand);
        Check(cardsOut == 2000 && NearRel(massOut, massIn, 1e-12) && delay.GetContentCards() == 0,
            "Puls: alle 2000 Karten kommen ganz an, Masse exakt (1e-12)");
        Check(onTime, "Puls: jede Karte kommt nach q oder q + 1 Schritten an");
        Check(NearAbs(mean, expected, 0.01), "Puls: mittlere Ankunft = mittlere Verzoegerung (0,01 Schritte)");
        Check(worstBand <= 2.0, "Puls: je Groessenfuenftel so viele spaete Karten wie die Summe der Reste (+-2)");
    }
    {
        // c) Flowsheet Inlet -> delay (standard settings) -> Outlet, 40 s,
        //    dt = 0.2 s: runs in particle mode, inflow = outflow + content,
        //    and in steady state the outlet equals the continuum.
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 1, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 2, 0);
        graph.GetInlet(in)->SetTimeProfile({ { 0.0, 2.0 }, { 100.0, 2.0 } });
        graph.AddConnection(in, 0, pr, 0);
        graph.AddConnection(pr, 0, out, 0);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 201, 40.0);
        graph.RunSimulations(1);
        const CSimulationResults continuum = *graph.GetDisplayResults(out, 0);
        graph.SetRepresentation(ERepresentation::Particles);
        Check(graph.GetUnitsWithoutParticleSupport().empty() && graph.RunSimulations(1) == 1,
            "Flowsheet Inlet -> Verzoegerung -> Outlet laeuft im Partikelmodus");
        const CSimulationResults& cards = *graph.GetDisplayResults(out, 0);
        const CSimulationResults& feed = *graph.GetDisplayResults(in, 0);
        const double dt = graph.GetGrid().GetDt();
        const auto* delay = dynamic_cast<const CSizeDependentDelayProcessor*>(graph.GetProcessor(pr));
        double massIn = 0.0, massOut = 0.0, massContinuum = 0.0, worstSteady = 0.0, worstStart = 0.0;
        for (size_t k = 0; k < 201; ++k)
        {
            massIn += feed.IntegrateOverSize(k) * dt;
            massOut += cards.IntegrateOverSize(k) * dt;
            massContinuum += continuum.IntegrateOverSize(k) * dt;
            const double diff = std::abs(cards.IntegrateOverSize(k) - continuum.IntegrateOverSize(k));
            if (k >= 40) // 8 s, longer than every delay of this distribution (about 5 s)
                worstSteady = std::max(worstSteady, diff);
            else
                worstStart = std::max(worstStart, diff);
        }
        const double balance = delay ? std::abs(massIn - massOut - delay->GetContentMass()) / massIn : 1.0;
        // Cards of different q come from different inflow steps, and each
        // group of equal q is balanced only within its own inflow step: per
        // output step the deviation is about one card per group (here some
        // 25 groups), not one card in total as in the vessel.
        std::printf("      Flowsheet: stationaer hoechstens %.2e kg/s (%.2f %%) neben dem Kontinuum, im Anlauf %.2e kg/s; "
            "Bilanz %.1e; Ablauf bis 40 s: Karten %.4f kg, Kontinuum %.4f kg\n",
            worstSteady, 100.0 * worstSteady / 2.0, worstStart, balance, massOut, massContinuum);
        Check(balance < 1e-12, "Flowsheet: Zulauf = Ablauf + Inhalt der Verzoegerung (1e-12)");
        Check(worstSteady <= 0.01 * 2.0, "Flowsheet: stationaer wie das Kontinuum (je Schritt hoechstens 1 %)");
    }
    {
        // d) Start flowsheet of the user interface: Inlet -> Merger ->
        //    Splitter 70/30 -> Outlet, 30 % back through the delay into the
        //    merger, with constant inflow. Runs in particle mode; in steady state
        //    the outlet carries the inflow, as in the continuum, and the cards in the loop stay
        //    within the limits per stream slice.
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string mg = graph.AddMergerNode("Merger1", 2, 1, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 2, 0);
        const std::string out = graph.AddOutletNode("Outlet1", 3, 0);
        const std::string pr = graph.AddProcessNode("Prozess1", 3, 1);
        graph.GetSplitter(sp)->SetFractions({ 0.7, 0.3 });
        graph.GetInlet(in)->SetTimeProfile({ { 0.0, 2.0 }, { 100.0, 2.0 } }); // constant, so that "steady" holds
        graph.AddConnection(in, 0, mg, 0);
        graph.AddConnection(mg, 0, sp, 0);
        graph.AddConnection(sp, 0, out, 0);
        graph.AddConnection(sp, 1, pr, 0);
        graph.AddConnection(pr, 0, mg, 1);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 201, 40.0);
        graph.RunSimulations(1);
        const CSimulationResults continuum = *graph.GetDisplayResults(out, 0);
        graph.SetRepresentation(ERepresentation::Particles);
        Check(graph.RunSimulations(1) == 1, "Start-Flowsheet mit Verzoegerung in der Rueckfuehrung laeuft im Partikelmodus");
        const CSimulationResults& cards = *graph.GetDisplayResults(out, 0);
        const double feed = 2.0;
        double worst = 0.0;
        for (size_t k = 150; k < 201; ++k) // 30 s and later: the loop has settled
            worst = std::max(worst, std::abs(cards.IntegrateOverSize(k) - continuum.IntegrateOverSize(k)) / feed);
        size_t maxLoopCards = 0;
        for (const CCardHistory::SSnapshot& snapshot : graph.GetCardHistory(mg, 0)->GetSnapshots())
            maxLoopCards = std::max(maxLoopCards, snapshot.sizes.size());
        std::printf("      Start-Flowsheet: Outlet ab 30 s hoechstens %.2f %% neben dem Kontinuum, Merger hoechstens %zu Karten\n",
            100.0 * worst, maxLoopCards);
        Check(worst < 0.01, "Start-Flowsheet: Outlet stationaer wie im Kontinuum (1 %)");
        Check(maxLoopCards <= 2 * graph.GetCardSettings().cardsPerSlice, "Start-Flowsheet: Merger innerhalb der Grenze 2 N");
    }

    return Summary();
}
