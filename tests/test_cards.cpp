// Karten im Strom und ihre Umrechnung aufs Groessenraster (Partikelmodus,
// Methoden-Dokument 13.20); Karten am Inlet an Quantilen mit
// Mindestzahl je Groessenbereich (13.21).
#include "FlowsheetGraph.h"
#include "CardProjection.h"
#include "CardDistribution.h"
#include <cmath>
#include <cstdio>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Summe w_i * q_i eines Stoffs - so integrieren auch Anzeige und Statistik.
static double Integrate(const CStreamSlice& slice, size_t compound, const std::vector<double>& weights)
{
    return slice.GetCompoundWeight(compound, weights);
}

// Massengewichteter Mittelwert der Rasterkoordinate (d bzw. ln d) einer
// projizierten Scheibe ueber alle Stoffe.
static double MeanCoordinate(const CStreamSlice& slice, const SSizeGrid& grid, const std::vector<double>& weights)
{
    double mass = 0.0, moment = 0.0;
    for (size_t s = 0; s < grid.numClasses; ++s)
    {
        const double m = weights[s] * slice.GetValue(s);
        const double d = grid.GetSizeAt(s);
        mass += m;
        moment += m * (grid.IsLogarithmic() ? std::log(d) : d);
    }
    return moment / mass;
}

// Groesse zum Massenanteil p (Q3(d) = p) durch Bisektion.
static double Quantile(const SSizeDistribution& distribution, double p)
{
    double lo = 1e-9, hi = 1e-2;
    for (int i = 0; i < 200; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (distribution.Cumulative(mid) < p ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

int main()
{
    // 1) Karten in der Stromscheibe: anlegen, Summen, leeren.
    {
        CStreamSlice slice;
        slice.Resize(300, 2, ERepresentation::Particles);
        Check(slice.HasCards() && slice.GetNumCards() == 0, "Scheibe im Partikelmodus startet ohne Karten");
        slice.AddCard(100e-6, 0, 2.0);
        slice.AddCard(50e-6, 1, 0.5);
        slice.AddCard(200e-6, std::vector<double>{ 0.25, 0.75 });
        slice.SetTemperature(350.0);
        Check(slice.GetNumCards() == 3, "drei Karten");
        Check(slice.GetCardMassFlow(0, 0) == 2.0 && slice.GetCardMassFlow(0, 1) == 0.0, "Karte aus einem Stoff: nur dieser Stoff");
        Check(slice.GetCardMassFlow(2) == 1.0 && slice.GetCardSize(2) == 200e-6, "gemischte Karte: Summe der Stoffe, Groesse");
        Check(slice.GetCardsMassFlow(0) == 2.25 && slice.GetCardsMassFlow(1) == 1.25 && slice.GetCardsMassFlow() == 3.5,
            "Massenstrom je Stoff und gesamt ueber alle Karten");
        slice.Clear();
        Check(slice.GetNumCards() == 0 && slice.GetCardsMassFlow() == 0.0 && slice.GetTemperature() == kRoomTemperature,
            "Clear: keine Karten, Raumtemperatur");
        Check(slice.HasCards(), "Clear behaelt die Darstellung");

        CStreamSlice continuum;
        continuum.Resize(10, 1);
        Check(!continuum.HasCards(), "Standard ist das Kontinuum");
    }

    // 2) Umrechnung aufs Raster: Masse bleibt je Stoff exakt erhalten -
    //    logarithmisch und linear, auch fuer Karten ausserhalb des Rasters.
    for (const SSizeGrid& grid : { SSizeGrid::Logarithmic(300, 1e-6, 1e-3), SSizeGrid::Linear(150, 0.0, 200e-6) })
    {
        const char* name = grid.IsLogarithmic() ? "log" : "linear";
        const std::vector<double> weights = grid.GetWeights();
        CRandom random;
        random.Seed(2101);
        CStreamSlice cards;
        cards.Resize(grid.numClasses, 2, ERepresentation::Particles);
        double expected[2] = { 0.0, 0.0 };
        double lnMoment = 0.0, mass = 0.0;
        for (int k = 0; k < 2000; ++k)
        {
            // Groessen gleichmaessig in der Rasterkoordinate, nur innerhalb
            const double u = random.Uniform();
            const double size = grid.IsLogarithmic() ? grid.sizeMin * std::pow(grid.sizeMax / grid.sizeMin, u)
                : grid.sizeMin + u * (grid.sizeMax - grid.sizeMin);
            const size_t compound = k % 2;
            const double massFlow = 0.001 + random.Uniform();
            cards.AddCard(size, compound, massFlow);
            expected[compound] += massFlow;
            mass += massFlow;
            lnMoment += massFlow * (grid.IsLogarithmic() ? std::log(size) : size);
        }
        CStreamSlice projected;
        ProjectCards(cards, grid, weights, projected);
        Check(!projected.HasCards() && projected.GetNumSizeClasses() == grid.numClasses && projected.GetNumCompounds() == 2,
            std::string(name) + ": Ergebnis ist eine Kontinuums-Scheibe passender Groesse");
        Check(NearRel(Integrate(projected, 0, weights), expected[0], 1e-12)
            && NearRel(Integrate(projected, 1, weights), expected[1], 1e-12),
            std::string(name) + ": Masse je Stoff exakt erhalten (2000 zufaellige Karten)");
        Check(NearRel(MeanCoordinate(projected, grid, weights), lnMoment / mass, 1e-12),
            std::string(name) + (grid.IsLogarithmic() ? ": mittleres ln d erhalten" : ": mittleres d erhalten"));

        // Karten unter d_min und ueber d_max: Masse auf der Randstuetzstelle.
        CStreamSlice outside;
        outside.Resize(grid.numClasses, 1, ERepresentation::Particles);
        outside.AddCard(grid.IsLogarithmic() ? 0.1e-6 : -5e-6, 0, 3.0);
        outside.AddCard(5e-3, 0, 1.0);
        outside.AddCard(0.0, 0, 0.5); // d = 0: auf dem log-Raster ungueltig -> unterer Rand
        ProjectCards(outside, grid, weights, projected);
        const size_t last = grid.numClasses - 1;
        Check(NearRel(weights[0] * projected.GetValue(0), 3.5, 1e-12) && NearRel(weights[last] * projected.GetValue(last), 1.0, 1e-12)
            && NearRel(Integrate(projected, 0, weights), 4.5, 1e-12),
            std::string(name) + ": Karten ausserhalb landen ganz am Rand, Masse bleibt");
    }

    // 3) Karte genau auf einer Stuetzstelle: alles dort; dazwischen anteilig.
    {
        const SSizeGrid grid = SSizeGrid::Logarithmic(31, 1e-6, 1e-3); // 10 je Dekade
        const std::vector<double> weights = grid.GetWeights();
        CStreamSlice cards, projected;
        cards.Resize(grid.numClasses, 1, ERepresentation::Particles);
        cards.AddCard(grid.GetSizeAt(10), 0, 2.0);
        ProjectCards(cards, grid, weights, projected);
        Check(NearRel(weights[10] * projected.GetValue(10), 2.0, 1e-9) && std::abs(projected.GetValue(11)) < 1e-9 * projected.GetValue(10),
            "Karte auf d_10: ganze Masse an d_10");

        cards.Clear();
        const double size = std::exp(0.3 * std::log(grid.GetSizeAt(21)) + 0.7 * std::log(grid.GetSizeAt(20)));
        cards.AddCard(size, 0, 1.0);
        ProjectCards(cards, grid, weights, projected);
        Check(NearRel(weights[20] * projected.GetValue(20), 0.7, 1e-9) && NearRel(weights[21] * projected.GetValue(21), 0.3, 1e-9),
            "Karte bei 30 % des Wegs (in ln d) von d_20 nach d_21: 70 % / 30 %");
    }

    // 4) Aufzeichnung: CStreamRecord rechnet Karten selbst um - Massenstrom,
    //    Stoffe, Temperatur kommen so in Kurven, Heatmap und Auswertung an.
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 300;
        grid.numTimeSteps = 3;
        grid.totalTime = 2.0;
        grid.compounds = { "Sand", "Coal" };
        grid.representation = ERepresentation::Particles;
        CStreamRecord record;
        record.Configure(grid);
        CStreamSlice cards;
        cards.Resize(grid.numSizeClasses, 2, ERepresentation::Particles);
        cards.AddCard(80e-6, 0, 1.5);
        cards.AddCard(120e-6, 1, 0.5);
        cards.SetTemperature(400.0);
        record.Record(1, cards);
        Check(NearRel(record.GetTotal().IntegrateOverSize(1), 2.0, 1e-12), "Aufzeichnung: Massenstrom = Summe der Karten");
        Check(NearRel(record.GetCompound(0).IntegrateOverSize(1), 1.5, 1e-12) && NearRel(record.GetCompound(1).IntegrateOverSize(1), 0.5, 1e-12),
            "Aufzeichnung: Massenstrom je Stoff");
        Check(record.GetTemperature()[1] == 400.0 && record.GetTotal().IntegrateOverSize(0) == 0.0,
            "Aufzeichnung: Temperatur uebernommen, andere Zeitpunkte unberuehrt");
    }

    // 5) Karten an Massen-Quantilen einer Verteilung sehen nach der
    //    Umrechnung aus wie das Kontinuum.
    {
        const SSizeGrid grid = SSizeGrid::Logarithmic(300, 1e-6, 1e-3);
        const std::vector<double> weights = grid.GetWeights();
        const SSizeDistribution distribution{ EDistributionType::Normal, 100e-6, 25e-6 };
        const int numCards = 2000;
        CStreamSlice cards, projected;
        cards.Resize(grid.numClasses, 1, ERepresentation::Particles);
        for (int k = 0; k < numCards; ++k)
            cards.AddCard(Quantile(distribution, (k + 0.5) / numCards), 0, 1.0 / numCards);
        ProjectCards(cards, grid, weights, projected);
        double massBelow = 0.0, continuumBelow = 0.0, meanCards = 0.0, meanContinuum = 0.0, continuumMass = 0.0;
        for (size_t s = 0; s < grid.numClasses; ++s)
        {
            const double d = grid.GetSizeAt(s);
            const double q = distribution.Density(d);
            meanCards += weights[s] * projected.GetValue(s) * d;
            meanContinuum += weights[s] * q * d;
            continuumMass += weights[s] * q;
            if (d <= 100e-6)
            {
                massBelow += weights[s] * projected.GetValue(s);
                continuumBelow += weights[s] * q;
            }
        }
        std::printf("      mittlere Groesse: Karten %.4f um, Kontinuum %.4f um; Anteil bis 100 um: %.5f / %.5f\n",
            meanCards * 1e6, meanContinuum / continuumMass * 1e6, massBelow, continuumBelow / continuumMass);
        Check(NearRel(meanCards, meanContinuum / continuumMass, 2e-4), "2000 Quantil-Karten: mittlere Groesse wie im Kontinuum (0,02 %)");
        Check(NearAbs(massBelow, continuumBelow / continuumMass, 2e-3), "2000 Quantil-Karten: Massenanteil bis 100 um wie im Kontinuum");
    }

    // 6) Graph: Im Partikelmodus startet keine Simulation, solange eine Unit
    //    keine Karten kann; im Kontinuum laeuft alles unveraendert.
    {
        CFlowsheetGraph graph;
        const std::string inlet = graph.AddInletNode("Inlet1", 0, 0);
        const std::string splitter = graph.AddSplitterNode("Splitter1", 2, 1, 0);
        const std::string outA = graph.AddOutletNode("Outlet1", 2, 0);
        const std::string outB = graph.AddOutletNode("Outlet2", 2, 1);
        const std::string process = graph.AddProcessNode("Prozess1", 1, 1); // Verzoegerung: noch ohne Karten
        graph.AddConnection(inlet, 0, splitter, 0);
        graph.AddConnection(splitter, 0, outA, 0);
        graph.AddConnection(process, 0, outB, 0);
        graph.AddConnection(splitter, 1, process, 0);
        graph.ConfigureGrid(50, 1e-6, 1e-3, 11, 10.0);

        Check(graph.GetRepresentation() == ERepresentation::Continuum && graph.GetUnitsWithoutParticleSupport().empty(),
            "Standard Kontinuum: keine Einwaende");
        Check(graph.RunSimulations(1) == 1, "Kontinuum: Simulation laeuft");
        const double before = graph.GetDisplayResults(outA, 0)->IntegrateOverSize(5);

        graph.SetRepresentation(ERepresentation::Particles);
        const std::vector<std::string> names = graph.GetUnitsWithoutParticleSupport();
        Check(names.size() == 1 && names[0] == "Prozess1 (Prozess)",
            "Partikelmodus: Verzoegerung gemeldet, Inlet, Splitter und Outlets nicht");
        Check(graph.RunSimulations(1) == 0, "Partikelmodus: Simulation nicht gestartet");
        const std::vector<std::string> warnings = graph.GetSimulationWarnings();
        std::printf("      Hinweis: %s\n", warnings.empty() ? "-" : warnings[0].c_str());
        Check(warnings.size() == 1 && warnings[0].find("nicht gestartet") != std::string::npos
            && warnings[0].find("Noch ohne Karten: Prozess1 (Prozess).") != std::string::npos,
            "ein Hinweis, der die fehlenden Units nennt");
        Check(graph.GetDisplayResults(outA, 0)->IntegrateOverSize(5) == before, "alte Ergebnisse bleiben stehen");

        graph.SetRepresentation(ERepresentation::Continuum);
        Check(graph.RunSimulations(1) == 1 && graph.GetSimulationWarnings().empty(), "zurueck im Kontinuum: laeuft wieder ohne Hinweise");
    }

    // 7) Nur Units, die Karten koennen (hier ein Outlet ohne Zulauf): Der
    //    Graph rechnet im Partikelmodus und zeichnet leere Scheiben auf.
    {
        CFlowsheetGraph graph;
        const std::string outlet = graph.AddOutletNode("Outlet1", 0, 0);
        graph.ConfigureGrid(50, 1e-6, 1e-3, 11, 10.0);
        graph.SetRepresentation(ERepresentation::Particles);
        Check(graph.RunSimulations(1) == 1 && graph.GetDisplayResults(outlet, 0)->IntegrateTotal() == 0.0,
            "Partikelmodus mit Outlet allein: laeuft, leerer Strom");
    }

    // 8) Quantilfunktion: Q3(Quantile(p)) = p fuer alle drei Verteilungen,
    //    auch weit in den Auslaeufern.
    {
        Check(NearAbs(StandardNormalQuantile(0.975), 1.959963984540054, 1e-14)
            && NearAbs(StandardNormalQuantile(1e-10), -6.361340902404056, 1e-12),
            "Standardnormal-Quantil: 97,5 % und 1e-10 (Tabellenwerte)");
        std::vector<SSizeDistribution> types = { { EDistributionType::Normal, 100e-6, 25e-6 },
            { EDistributionType::LogNormal, 100e-6, 0.4 }, { EDistributionType::RRSB, 65e-6, 2.0 },
            { EDistributionType::GGS, 200e-6, 2.0 }, { EDistributionType::Table } };
        types.back().points = { { 40e-6, 0.0 }, { 63e-6, 0.03 }, { 100e-6, 0.5 }, { 160e-6, 0.97 }, { 250e-6, 1.0 } };
        double worst = 0.0;
        for (const SSizeDistribution& distribution : types)
            for (double p : { 1e-9, 1e-4, 0.02, 0.3, 0.5, 0.9, 0.999, 1.0 - 1e-7 })
                worst = std::max(worst, std::abs(distribution.Cumulative(distribution.Quantile(p)) - p) / std::min(p, 1.0 - p));
        std::printf("      groesster relativer Fehler von Q3(Quantile(p)): %.1e\n", worst);
        // Schranke relativ zu min(p, 1 - p): GGS kurz vor d_max (p = 1 - 1e-7)
        // erreicht absolut 5e-16, also Rundung in der letzten Stelle.
        Check(worst < 1e-8, "Quantile: Q3(Quantile(p)) = p fuer Normal, Log-Normal, RRSB, GGS, Tabelle (relativ 1e-8)");

        // Karten fuer GGS und Tabelle: Masse exakt, alle Karten innerhalb der Verteilung.
        for (size_t t = 3; t < types.size(); ++t)
        {
            const SCardPlacement placement = PlaceCardsAtQuantiles(types[t], SCardSettings(), 1e-6, 1e-3);
            double sum = 0.0;
            for (double m : placement.massFractions)
                sum += m;
            const double top = t == 3 ? 200e-6 : 250e-6;
            std::printf("      %s: %zu belegte Bereiche, N_min = %zu, Karten %.3g ... %.4g um\n", t == 3 ? "GGS" : "Tabelle",
                placement.numOccupied, placement.minPerRange, placement.sizes.front() * 1e6, placement.sizes.back() * 1e6);
            Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.sizes.back() <= top
                && placement.sizes.front() >= (t == 3 ? 1e-6 : 40e-6),
                std::string(t == 3 ? "GGS" : "Tabelle") + ": 2000 Karten, Masse 1, alle innerhalb der Verteilung");
        }
    }

    // 9) Beispiel aus dem Methoden-Dokument 13.19: Log-Normal d50 = 100 um,
    //    sigma_ln = 0,4, 200 Karten, f = 25 %, Bereiche 12,5-25 ... 200-400 um
    //    -> 10 / 16 / 79 / 79 / 16 Karten.
    {
        SCardSettings settings;
        settings.cardsPerSlice = 200;
        settings.reservedFraction = 0.25;
        settings.rangeAnchor = 12.5e-6;
        const SSizeDistribution distribution{ EDistributionType::LogNormal, 100e-6, 0.4 };
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6, 1e-3);
        std::vector<size_t> counts;
        for (const SCardRange& range : placement.ranges)
            if (range.numCards > 0)
                counts.push_back(range.numCards);
        const std::vector<size_t> expected = { 10, 16, 79, 79, 16 };
        size_t firstWithCards = 0;
        while (firstWithCards < placement.ranges.size() && placement.ranges[firstWithCards].numCards == 0)
            ++firstWithCards;
        Check(counts == expected && placement.numOccupied == 5 && placement.minPerRange == 10
            && NearRel(placement.ranges[firstWithCards].lower, 12.5e-6, 1e-12),
            "Beispiel 13.19: 10 / 16 / 79 / 79 / 16 Karten ab 12,5 um, N_min = 10, 5 belegte Bereiche");
        Check(placement.ranges[firstWithCards].massFraction < 1e-3 && placement.ranges[firstWithCards].numberFraction > 1e-3,
            "Beispiel 13.19: 12,5-25 um belegt wegen der Anzahl, nicht wegen der Masse");
        double sum = 0.0;
        bool ascending = true, equalInRange = true;
        for (size_t k = 0; k < placement.sizes.size(); ++k)
        {
            sum += placement.massFractions[k];
            if (k > 0)
            {
                ascending = ascending && placement.sizes[k] > placement.sizes[k - 1];
                if (GetCardRangeIndex(placement.sizes[k], settings) == GetCardRangeIndex(placement.sizes[k - 1], settings)
                    && placement.sizes[k] > 12.5e-6 && placement.sizes[k] < 400e-6)
                    equalInRange = equalInRange && placement.massFractions[k] == placement.massFractions[k - 1];
            }
        }
        Check(placement.sizes.size() == 200 && NearAbs(sum, 1.0, 1e-12), "Beispiel 13.19: genau 200 Karten, Masse 1");
        Check(ascending && equalInRange, "Beispiel 13.19: Groessen aufsteigend, gleiche Masse innerhalb eines Bereichs");
    }

    // 10) Standard-Inlet (Normal 100 um +- 25 um) mit den Startwerten: Tabelle
    //     der Bereiche fuer das Methoden-Dokument.
    {
        const SCardSettings settings; // N 2000, f 20 %, Faktor 2 ab 1 um
        const SSizeDistribution distribution{ EDistributionType::Normal, 100e-6, 25e-6 };
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6, 1e-3);
        std::printf("      Bereich [um]      Masse        Anzahl      belegt  Karten\n");
        for (const SCardRange& range : placement.ranges)
            if (range.massFraction > 1e-12 || range.numCards > 0)
                std::printf("      %7.4g - %-7.4g %.3e   %.3e   %s     %zu\n", range.lower * 1e6, range.upper * 1e6,
                    range.massFraction, range.numberFraction, range.occupied ? "ja  " : "nein", range.numCards);
        std::printf("      B_belegt = %zu, N_min = %zu, kleinste Karte %.3g um, groesste %.4g um\n", placement.numOccupied,
            placement.minPerRange, placement.sizes.front() * 1e6, placement.sizes.back() * 1e6);
        double sum = 0.0;
        for (double m : placement.massFractions)
            sum += m;
        Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.sizes.front() > 0.0,
            "Normalverteilung: 2000 Karten, Masse 1, alle Groessen > 0");
        Check(placement.minPerRange * placement.numOccupied >= 400 && placement.minPerRange * placement.numOccupied < 400 + placement.numOccupied,
            "Normalverteilung: Mindestzahlen zusammen f * N = 400 (aufgerundet)");
    }

    // 11) Inlet im Flowsheet: Karten tragen den Massenstrom exakt in jedem
    //     Schritt, die Anzeige gleicht dem Kontinuum.
    {
        CFlowsheetGraph graph;
        const std::string inlet = graph.AddInletNode("Inlet1", 0, 0);
        const std::string outlet = graph.AddOutletNode("Outlet1", 1, 0);
        graph.AddConnection(inlet, outlet);
        graph.ConfigureGrid(300, 1e-6, 1e-3, 201, 40.0); // Standard-Profil, dt = 0,2 s
        Check(graph.RunSimulations(1) == 1, "Kontinuum: Inlet -> Outlet");
        const CSimulationResults continuum = *graph.GetDisplayResults(outlet, 0);
        graph.SetRepresentation(ERepresentation::Particles);
        Check(graph.RunSimulations(1) == 1 && graph.GetSimulationWarnings().empty(), "Partikelmodus: Inlet -> Outlet laeuft ohne Hinweise");
        const CSimulationResults& cards = *graph.GetDisplayResults(outlet, 0);
        const CInletUnit* unit = graph.GetInlet(inlet);
        double worstMass = 0.0, worstShape = 0.0;
        const SSizeGrid sizes = graph.GetGrid().GetSizeGrid();
        for (size_t k = 0; k < 201; ++k)
        {
            const double massFlow = unit->GetMassFlowAtTime(k * 0.2);
            if (massFlow <= 0.0)
                continue;
            worstMass = std::max(worstMass, std::abs(cards.IntegrateOverSize(k) - massFlow) / massFlow);
            // Verteilungssumme an jeder Stuetzstelle, Karten gegen Kontinuum
            double sumCards = 0.0, sumContinuum = 0.0;
            for (size_t s = 0; s < 300; ++s)
            {
                sumCards += sizes.GetWeight(s) * cards.GetValue(k, s);
                sumContinuum += sizes.GetWeight(s) * continuum.GetValue(k, s);
                worstShape = std::max(worstShape, std::abs(sumCards - sumContinuum) / massFlow);
            }
        }
        std::printf("      Massenstrom: groesste relative Abweichung %.1e; Verteilungssumme Karten vs. Kontinuum: %.1e\n",
            worstMass, worstShape);
        Check(unit->GetCardPlacement().sizes.size() == 2000, "Inlet erzeugt 2000 Karten je Zeitschritt");
        Check(worstMass < 1e-12, "Massenstrom am Outlet = Inlet-Profil in jedem Schritt (relativ 1e-12)");
        Check(worstShape < 2e-3, "Verteilungssumme wie im Kontinuum (Abweichung < 0,2 % des Massenstroms)");
    }

    // 12) Warnung bei zu wenigen Karten je Bereich und bei ungueltiger Verteilung.
    {
        CFlowsheetGraph graph;
        const std::string inlet = graph.AddInletNode("Inlet1", 0, 0);
        graph.AddConnection(inlet, graph.AddOutletNode("Outlet1", 1, 0));
        graph.ConfigureGrid(100, 1e-6, 1e-3, 11, 10.0);
        graph.SetRepresentation(ERepresentation::Particles);
        SCardSettings few;
        few.cardsPerSlice = 20;
        graph.SetCardSettings(few);
        graph.RunSimulations(1);
        const std::vector<std::string> warnings = graph.GetSimulationWarnings();
        std::printf("      Hinweis: %s\n", warnings.empty() ? "-" : warnings[0].c_str());
        Check(warnings.size() == 1 && warnings[0].find("Nur 2 Karten je belegtem") != std::string::npos,
            "20 Karten: Warnung zu wenige Karten je Bereich (Standard-Inlet: 3 belegte Bereiche, N_min = 2)");
        graph.SetCardSettings(SCardSettings());
        graph.GetInlet(inlet)->SetSizeDistribution({ EDistributionType::LogNormal, 100e-6, 0.0 });
        graph.RunSimulations(1);
        const std::vector<std::string> invalid = graph.GetSimulationWarnings();
        Check(invalid.size() == 1 && invalid[0].find("Keine Karten erzeugt") != std::string::npos,
            "ungueltige Verteilung: Warnung keine Karten");
    }

    // 13) RRSB mit n = 2: die Anzahl waechst zu kleinen Groessen ohne Grenze -
    //     gezaehlt wird nur im Rasterbereich, Masse bleibt exakt.
    {
        const SCardSettings settings;
        const SSizeDistribution distribution{ EDistributionType::RRSB, 65e-6, 2.0 };
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6, 1e-3);
        double sum = 0.0;
        for (double m : placement.massFractions)
            sum += m;
        std::printf("      RRSB n = 2: %zu belegte Bereiche, N_min = %zu, kleinste Karte %.3g um\n",
            placement.numOccupied, placement.minPerRange, placement.sizes.front() * 1e6);
        Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.numOccupied <= 12,
            "RRSB n = 2: 2000 Karten, Masse 1, belegte Bereiche begrenzt");
    }

    // 14) Verteilung aus den Karten (Kartenansicht A-E): feine Klassen,
    //     Dichte je ln d, Summenkurve und Quantile.
    {
        const SCardSettings settings;
        const int k32 = GetCardRangeIndex(40e-6, settings); // Bereich 32-64 um
        const std::vector<double> fine = MakeFineClassBounds(k32, k32 + 2, 4, settings);
        const double ratio = std::pow(settings.rangeFactor, 0.25);
        bool geometric = fine.size() == 13;
        for (size_t i = 1; geometric && i < fine.size(); ++i)
            geometric = NearAbs(fine[i] / fine[i - 1], ratio, 1e-12);
        Check(geometric && NearAbs(fine[4], GetCardRangeLower(k32 + 1, settings), 1e-18),
            "feine Klassen: 3 Bereiche x 4 = 12 Klassen, gleich breit in ln d, Bereichsgrenzen enthalten");

        const std::vector<double> sizes = { 130e-6, 33e-6, 200e-6, 40e-6, 70e-6 };
        const std::vector<double> masses = { 3.0, 1.0, 3.0, 1.0, 2.0 };
        const std::vector<SCardClass> classes = BinCards(sizes, masses, MakeFineClassBounds(k32, k32 + 2, 1, settings));
        std::printf("      Klassen: %zu/%zu/%zu Karten, Anteile %.2f/%.2f/%.2f\n", classes[0].cards, classes[1].cards,
            classes[2].cards, classes[0].massShare, classes[1].massShare, classes[2].massShare);
        Check(classes.size() == 3 && classes[0].cards == 2 && classes[1].cards == 1 && classes[2].cards == 2
            && NearAbs(classes[0].massShare, 0.2, 1e-12) && NearAbs(classes[2].massShare, 0.6, 1e-12),
            "BinCards: Karten und Massenanteile je Bereich");
        Check(NearAbs(classes[0].GetDensity(), 0.2 / std::log(settings.rangeFactor), 1e-12),
            "Dichte q3*(ln d) = Massenanteil / Klassenbreite in ln d");

        const SCardCumulative cumulative = MakeCardCumulative(sizes, masses);
        Check(cumulative.sizes.front() == 33e-6 && NearAbs(cumulative.q3.back(), 1.0, 1e-12)
            && GetCardQuantile(cumulative, 0.1) == 33e-6 && GetCardQuantile(cumulative, 0.5) == 130e-6
            && GetCardQuantile(cumulative, 0.9) == 200e-6,
            "Summenkurve der Karten: sortiert, endet bei 1, d10/d50/d90 = 33/130/200 um");

        // Gleichverteilt in ln d von 1 bis e^2: Q3 = 0, 1/2, 1 und d50 = e.
        const std::vector<double> nodes = { 1.0, std::exp(1.0), std::exp(2.0) };
        const std::vector<double> q = CumulativeFromDensity(nodes, { 0.5, 0.5, 0.5 });
        Check(NearAbs(q[1], 0.5, 1e-12) && NearAbs(q[2], 1.0, 1e-12)
            && NearAbs(GetCurveQuantile(nodes, q, 0.5), std::exp(1.0), 1e-12)
            && NearAbs(GetCurveQuantile(nodes, q, 0.25), std::exp(0.5), 1e-12),
            "Summenkurve einer Dichte (Trapezregel in ln d) und ihre Quantile");

        // Echte Platzierung (RRSB wie in 13): alle Karten einsortiert, Masse 1.
        const SCardPlacement placement = PlaceCardsAtQuantiles({ EDistributionType::RRSB, 65e-6, 2.0 }, settings, 1e-6, 1e-3);
        const int first = GetCardRangeIndex(placement.sizes.front(), settings);
        const int last = GetCardRangeIndex(placement.sizes.back(), settings);
        size_t cards = 0;
        double share = 0.0;
        for (const SCardClass& c : BinCards(placement.sizes, placement.massFractions, MakeFineClassBounds(first, last, 8, settings)))
        {
            cards += c.cards;
            share += c.massShare;
        }
        Check(cards == placement.sizes.size() && NearAbs(share, 1.0, 1e-12),
            "RRSB-Karten in 8 Klassen je Bereich: alle Karten gezaehlt, Massenanteile ergeben 1");
    }

    return Summary();
}
