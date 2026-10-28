// Merger und Splitter im Partikelmodus (Methoden-Dokument 13.18, 13.22):
// Karten aneinanderhaengen, Konto-Verfahren, Vergleich mit dem Kontinuum.
#include "FlowsheetGraph.h"
#include "CardPlacement.h"
#include "CardDistribution.h"
#include "CardLimits.h"
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

    // 6) Grenzen je Stromscheibe (Methoden-Dokument 13.24): teilen unter N_min,
    //    zusammenlegen ueber 2 x Sollzahl; die Masse bleibt genau erhalten,
    //    beim Zusammenlegen auch die Partikelzahl.
    {
        const SCardSettings settings; // N = 2000, f = 20 %, Obergrenze 2 x Sollzahl
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
                v[k] = s.GetCardMassFlow(k);
            return v;
        };
        const auto numberOf = [](const CStreamSlice& s)
        {
            double n = 0.0;
            for (size_t k = 0; k < s.GetNumCards(); ++k)
                n += s.GetCardMassFlow(k) / std::pow(s.GetCardSize(k), 3.0);
            return n;
        };
        // Jeder Bereich zwischen Unter- und Obergrenze (nach den Grenzen der Scheibe selbst).
        const auto withinLimits = [&](const CStreamSlice& s)
        {
            bool ok = true;
            for (const SSliceRange& r : ComputeSliceLimits(sizesOf(s), massesOf(s), settings).ranges)
                ok = ok && r.cards >= r.minCards && r.cards <= r.maxCards;
            return ok;
        };
        // Masse je Groesse: Teilen darf nur Stuecke gleicher Groesse erzeugen.
        const auto massBySize = [](const CStreamSlice& s)
        {
            std::vector<std::pair<double, double>> v;
            for (size_t k = 0; k < s.GetNumCards(); ++k)
                v.push_back({ s.GetCardSize(k), s.GetCardMassFlow(k) });
            std::sort(v.begin(), v.end());
            std::vector<std::pair<double, double>> merged;
            for (const auto& entry : v)
                if (!merged.empty() && merged.back().first == entry.first)
                    merged.back().second += entry.second;
                else
                    merged.push_back(entry);
            return merged;
        };

        // a) Karten eines Inlets liegen genau auf den Sollzahlen: nichts zu tun.
        CStreamSlice inlet;
        inlet.Resize(300, 1, ERepresentation::Particles);
        AddCards(inlet, standard, 2000, 0, 1.0);
        CStreamSlice same = inlet;
        const SLimitCounts none = ApplySliceLimits(same, settings);
        bool identical = same.GetNumCards() == inlet.GetNumCards();
        for (size_t k = 0; identical && k < inlet.GetNumCards(); ++k)
            identical = same.GetCardSize(k) == inlet.GetCardSize(k) && same.GetCardMassFlow(k, 0) == inlet.GetCardMassFlow(k, 0);
        Check(identical && none.added == 0 && none.removed == 0, "Grenzen: Inlet-Karten (Sollzahlen) bleiben unveraendert");

        // b) Teilen: 20 Karten (jede 100. des Inlets, wie hinter einem 1-%-Splitterausgang).
        CStreamSlice few;
        few.Resize(300, 1, ERepresentation::Particles);
        for (size_t k = 50; k < inlet.GetNumCards(); k += 100)
            few.AddCard(inlet.GetCardSize(k), 0, inlet.GetCardMassFlow(k, 0));
        CStreamSlice split = few;
        const SLimitCounts added = ApplySliceLimits(split, settings);
        const SSliceLimits splitLimits = ComputeSliceLimits(sizesOf(split), massesOf(split), settings);
        std::printf("      Teilen: %zu Karten -> %zu (N_min = %zu in %zu belegten Bereichen)\n", few.GetNumCards(),
            split.GetNumCards(), splitLimits.minPerRange, splitLimits.numOccupied);
        Check(withinLimits(split) && added.added == split.GetNumCards() - few.GetNumCards() && added.removed == 0,
            "Teilen: jeder belegte Bereich bekommt N_min Karten");
        const auto beforeSizes = massBySize(few), afterSizes = massBySize(split);
        bool sameMassBySize = beforeSizes.size() == afterSizes.size();
        for (size_t i = 0; sameMassBySize && i < beforeSizes.size(); ++i)
            sameMassBySize = beforeSizes[i].first == afterSizes[i].first
                && NearRel(afterSizes[i].second, beforeSizes[i].second, 1e-12);
        Check(sameMassBySize && NearRel(split.GetCardsMassFlow(), few.GetCardsMassFlow(), 1e-14),
            "Teilen: nur Stuecke gleicher Groesse, Masse je Groesse genau erhalten (Raster unveraendert)");
        // 90 gleich schwere Karten in einem Bereich, N_min = 134: die 44
        // zusaetzlichen Stuecke verteilen sich gleichmaessig ueber den Bereich.
        {
            SCardSettings one = settings;
            one.cardsPerSlice = 670; // ein belegter Bereich: N_min = ceil(0,2 * 670) = 134
            CStreamSlice even;
            even.Resize(300, 1, ERepresentation::Particles);
            for (int i = 0; i < 90; ++i)
                even.AddCard(130e-6 + 1e-6 * i, 0, 1.0);
            ApplySliceLimits(even, one);
            size_t lowerHalf = 0;
            for (size_t k = 0; k < even.GetNumCards(); ++k)
                lowerHalf += even.GetCardSize(k) < 174.5e-6 ? 1 : 0;
            std::printf("      Teilen gleichmaessig: 90 -> %zu Karten, davon %zu in der kleineren Haelfte (ideal 67)\n",
                even.GetNumCards(), lowerHalf);
            Check(even.GetNumCards() == 134 && lowerHalf >= 66 && lowerHalf <= 68,
                "Teilen: zusaetzliche Stuecke gleichmaessig ueber den Bereich, nicht nur die kleinsten Karten");
        }

        // c) Zusammenlegen: 10 Scheiben aus zwei verschiedenen Verteilungen in einer.
        const SSizeDistribution coarse{ EDistributionType::LogNormal, 120e-6, 0.3 };
        CStreamSlice many;
        many.Resize(300, 1, ERepresentation::Particles);
        for (int i = 0; i < 10; ++i)
            AddCards(many, i % 2 == 0 ? standard : coarse, 2000, 0, 0.1);
        CStreamSlice merged = many;
        const SLimitCounts removed = ApplySliceLimits(merged, settings);
        const SCardCumulative q0 = MakeCardCumulative(sizesOf(many), massesOf(many));
        const SCardCumulative q1 = MakeCardCumulative(sizesOf(merged), massesOf(merged));
        double worstQuantile = 0.0;
        for (double f : { 0.1, 0.5, 0.9 })
            worstQuantile = std::max(worstQuantile, std::abs(GetCardQuantile(q1, f) / GetCardQuantile(q0, f) - 1.0));
        std::printf("      Zusammenlegen: %zu Karten -> %zu, d10/d50/d90 um hoechstens %.3f %% verschoben\n",
            many.GetNumCards(), merged.GetNumCards(), 100.0 * worstQuantile);
        Check(withinLimits(merged) && removed.removed == many.GetNumCards() - merged.GetNumCards() && removed.added == 0,
            "Zusammenlegen: jeder Bereich hoechstens 2 x Sollzahl");
        Check(NearRel(merged.GetCardsMassFlow(), many.GetCardsMassFlow(), 1e-12) && NearRel(numberOf(merged), numberOf(many), 1e-10),
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
        ApplySliceLimits(mixedMerged, settings);
        bool pure = true;
        for (size_t k = 0; k < mixedMerged.GetNumCards(); ++k)
            pure = pure && (mixedMerged.GetCardMassFlow(k, 0) == 0.0 || mixedMerged.GetCardMassFlow(k, 1) == 0.0);
        Check(pure && mixedMerged.GetNumCards() < mixed.GetNumCards()
            && NearRel(mixedMerged.GetCardsMassFlow(0), mixed.GetCardsMassFlow(0), 1e-12)
            && NearRel(mixedMerged.GetCardsMassFlow(1), mixed.GetCardsMassFlow(1), 1e-12),
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
            ApplySliceLimits(loop[0], settings);
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            splitter.Step({ k, 0.1 * k, 0.1 }, { &loop[0] }, outputs);
            for (CStreamSlice& o : outputs)
                ApplySliceLimits(o, settings);
            recycled = outputs[1];
            loopCards = loop[0].GetNumCards();
            maxLoopCards = std::max(maxLoopCards, loopCards);
            outMass = outputs[0].GetCardsMassFlow();
        }
        std::printf("      Rueckfuehrung R = 0,9 mit Grenzen: %zu Karten im Kreis (hoechstens %zu; ohne Grenzen 20000), "
            "Austrag %.6f kg/s\n", loopCards, maxLoopCards, outMass);
        Check(maxLoopCards <= 2 * settings.cardsPerSlice + 10 && NearAbs(outMass, 1.0, 0.01),
            "Rueckfuehrung R = 0,9: Grenzen deckeln die Karten im Kreis bei etwa 2 N, Austrag = Zulauf");

        // f) Im Flowsheet: Inlet -> OU-Splitter 30/70 -> 2 Outlets. Teilen
        //    aendert die Ergebnisse auf dem Raster nicht.
        CFlowsheetGraph graph;
        const std::string in = graph.AddInletNode("Inlet1", 0, 0);
        const std::string sp = graph.AddSplitterNode("Splitter1", 2, 1, 0);
        const std::string a = graph.AddOutletNode("Outlet1", 2, 0);
        const std::string b = graph.AddOutletNode("Outlet2", 2, 1);
        graph.GetSplitter(sp)->SetStochastic(true);
        graph.GetSplitter(sp)->SetFractions({ 0.3, 0.7 });
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
                    masses[k] += snapshot.massFlows[k * numC + c];
            for (const SSliceRange& r : ComputeSliceLimits(snapshot.sizes, masses, on).ranges)
                allWithin = allWithin && r.cards >= r.minCards && r.cards <= r.maxCards;
        }
        const SUnitTiming& splitTiming = graph.GetSimulationTiming().units[1];
        std::printf("      Flowsheet: Splitter je Schritt +%.1f Karten durch Teilen, -%.1f durch Zusammenlegen; "
            "Ergebnis Outlet1 weicht um %.2e (relativ) ab\n", splitTiming.GetAddedPerStep(), splitTiming.GetRemovedPerStep(),
            largest > 0.0 ? worstDiff / largest : 0.0);
        Check(allWithin && splitTiming.cardsAdded > 0.0, "Flowsheet: jeder Splitterausgang in jedem Schritt innerhalb der Grenzen");
        Check(largest > 0.0 && worstDiff <= 1e-12 * largest, "Flowsheet: Teilen aendert das Ergebnis am Outlet nicht (1e-12)");
    }

    return Summary();
}
