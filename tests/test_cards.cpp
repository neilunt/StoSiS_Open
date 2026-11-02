// Karten im Strom und ihre Umrechnung aufs Groessenraster (Partikelmodus,
// Methoden-Dokument 13.20); Karten am Inlet in Streifen gleicher Flaeche
// unter der Kartendichte (13.26).
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

        // Karten fuer GGS und Tabelle: Masse exakt, alle Karten innerhalb der
        // Verteilung (GGS: 0 bis 200 um, Tabelle: 40 bis 250 um).
        for (size_t t = 3; t < types.size(); ++t)
        {
            const SCardPlacement placement = PlaceCardsAtQuantiles(types[t], SCardSettings(), 1e-6);
            double sum = 0.0;
            for (double m : placement.massFractions)
                sum += m;
            const double top = t == 3 ? 200e-6 : 250e-6;
            std::printf("      %s: Karten %.3g ... %.4g um, Raender %zu / %zu Karten\n", t == 3 ? "GGS" : "Tabelle",
                placement.sizes.front() * 1e6, placement.sizes.back() * 1e6, placement.fineCards, placement.coarseCards);
            Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.sizes.back() <= top
                && placement.sizes.front() >= (t == 3 ? 0.0 : 40e-6) && placement.sizes.front() > 0.0,
                std::string(t == 3 ? "GGS" : "Tabelle") + ": 2000 Karten, Masse 1, alle innerhalb der Verteilung");
        }
    }

    // 9) Beispiel aus dem Methoden-Dokument 13.26: Log-Normal d50 = 100 um,
    //    sigma_ln = 0,5, 1000 Karten. Vergleichswerte aus einer unabhaengigen
    //    Rechnung mit feiner Tabelle (Karten unter 20 um / ueber 300 um,
    //    schwerste Karte, meiste Partikel je Karte).
    {
        const SSizeDistribution distribution{ EDistributionType::LogNormal, 100e-6, 0.5 };
        const auto place = [&](ECardDensityRule rule, double f, double gamma)
        {
            SCardSettings settings;
            settings.cardsPerSlice = 1000;
            settings.rule = rule;
            settings.numberShare = f;
            settings.exponent = gamma;
            return PlaceCardsAtQuantiles(distribution, settings, 1e-6);
        };
        const auto edges = [](const SCardPlacement& placement, size_t& fine, size_t& coarse)
        {
            fine = coarse = 0;
            for (double d : placement.sizes)
            {
                fine += d < 20e-6 ? 1 : 0;
                coarse += d > 300e-6 ? 1 : 0;
            }
        };
        size_t fine = 0, coarse = 0;

        // Summe f = 0,2, gamma = 0,6: 34 / 37 Karten, hoechstens 1,50-fache Masse und 2,6-fache Partikelzahl.
        const SCardPlacement flat = place(ECardDensityRule::Sum, 0.2, 0.6);
        edges(flat, fine, coarse);
        double sum = 0.0;
        bool ascending = true;
        for (size_t k = 0; k < flat.sizes.size(); ++k)
        {
            sum += flat.massFractions[k];
            ascending = ascending && (k == 0 || flat.sizes[k] > flat.sizes[k - 1]);
        }
        std::printf("      Summe 0,2, gamma 0,6: unter 20 um %zu, ueber 300 um %zu Karten; Masse je Karte hoechstens %.3f, "
            "Partikel je Karte hoechstens %.3f x Mittel\n", fine, coarse, flat.maxMassRatio, flat.maxNumberRatio);
        Check(flat.sizes.size() == 1000 && NearAbs(sum, 1.0, 1e-12) && ascending, "Kartendichte: 1000 Karten, Masse 1, aufsteigend");
        Check(fine >= 32 && fine <= 36 && coarse >= 35 && coarse <= 39,
            "Summe 0,2, gamma 0,6: Raender 34 / 37 Karten wie in der Vergleichsrechnung (+-2)");
        Check(NearAbs(flat.maxMassRatio, 1.50, 0.03) && NearAbs(flat.maxNumberRatio, 2.60, 0.06),
            "Summe 0,2, gamma 0,6: schwerste Karte 1,50, meiste Partikel 2,6 x Mittel");

        // Standard (Summe 0,5, gamma 0,8): feinste 5 % der Partikel und groebste 5 % der Masse je 39 Karten
        // (bei f = 0,5 liegen beide Raender spiegelgleich).
        const SCardPlacement standardCards = place(ECardDensityRule::Sum, 0.5, 0.8);
        std::printf("      Standard: %zu Karten unter %.2f um (5 %% der Partikel), %zu ueber %.1f um (5 %% der Masse)\n",
            standardCards.fineCards, standardCards.fineSize * 1e6, standardCards.coarseCards, standardCards.coarseSize * 1e6);
        Check(standardCards.fineCards >= 37 && standardCards.fineCards <= 41 && standardCards.coarseCards >= 37
            && standardCards.coarseCards <= 41 && NearRel(standardCards.fineSize, 20.8e-6, 0.01)
            && NearRel(standardCards.coarseSize, 227.6e-6, 0.002),
            "Standard: Raender je 39 Karten an den Grenzen 20,8 und 227,6 um (+-2)");

        // Randzellen ohne Auslaeufer: Die Kartendichte hat an der ersten und
        // letzten Zelle keinen Knick (sigma = 1, f = 1, gamma = 0,3 macht
        // kleine Dichten gut sichtbar). Unter d_min ist die Anzahl mit echter
        // Groesse groesser als die gezaehlte, darueber gleich.
        {
            SCardSettings settings;
            settings.numberShare = 1.0;
            settings.exponent = 0.3;
            const SCardPlacement wide = PlaceCardsAtQuantiles({ EDistributionType::LogNormal, 100e-6, 1.0 }, settings, 1e-6);
            const SCardDensityCurve& c = wide.curve;
            const size_t last = c.cards.size() - 1;
            const double firstStep = c.cards[0] / c.cards[1], lastStep = c.cards[last] / c.cards[last - 1];
            bool trueAbove = true, trueBelow = true;
            for (size_t k = 0; k < c.sizes.size(); ++k)
                if (c.sizes[k] > 1e-6)
                    trueAbove = trueAbove && c.numberTrue[k] == c.number[k];
                else
                    trueBelow = trueBelow && c.numberTrue[k] > c.number[k];
            std::printf("      Randzellen: Kartendichte erste / zweite Zelle %.3f, letzte / vorletzte %.3f\n", firstStep, lastStep);
            Check(firstStep > 0.9 && firstStep < 1.1 && lastStep > 0.9 && lastStep < 1.1,
                "Kartendichte ohne Knick an der ersten und letzten Zelle (Nachbarn auf 10 % gleich)");
            Check(trueAbove && trueBelow, "Anzahl mit echter Groesse: unter d_min groesser als gezaehlt, darueber gleich");
        }

        // Ohne Abflachung: Schranken 1/(1 - f) und 1/f (Summe), 2 und 2 (Quadratisch bei f = 0,5).
        const SCardPlacement sum05 = place(ECardDensityRule::Sum, 0.5, 1.0);
        const SCardPlacement quadratic = place(ECardDensityRule::Quadratic, 0.5, 1.0);
        std::printf("      ohne Abflachung: Summe 0,5 %.3f / %.3f, Quadratisch 0,5 %.3f / %.3f x Mittel\n",
            sum05.maxMassRatio, sum05.maxNumberRatio, quadratic.maxMassRatio, quadratic.maxNumberRatio);
        Check(sum05.maxMassRatio <= 2.0 * 1.005 && sum05.maxNumberRatio <= 2.0 * 1.005
            && quadratic.maxMassRatio <= 2.0 * 1.005 && quadratic.maxNumberRatio <= 2.0 * 1.005,
            "ohne Abflachung: Summe und Quadratisch bei f = 0,5 hoechstens doppelte Masse und Partikelzahl");

        // Produkt f = 0,5: ein Buckel mit dem Median von sqrt(Masse * Anzahl), 100 um * exp(-1,5 sigma^2) = 68,73 um.
        const SCardPlacement product = place(ECardDensityRule::Product, 0.5, 1.0);
        std::printf("      Produkt 0,5: Karte 500 bei %.2f um\n", product.sizes[499] * 1e6);
        Check(NearRel(std::sqrt(product.sizes[499] * product.sizes[500]), 100e-6 * std::exp(-1.5 * 0.25), 0.005),
            "Produkt f = 0,5: mittlere Karte beim Median von sqrt(Masse * Anzahl), 68,7 um");

        // Anteil je Karte an der Kartendichte (Grenzen je Stromscheibe): am Inlet etwa 1/N.
        SCardSettings settings;
        settings.cardsPerSlice = 1000;
        const std::vector<double> shares = ComputeCardShares(standardCards.sizes, standardCards.massFractions, settings, 1e-6);
        double lowest = 1.0, highest = 0.0, worstInner = 0.0;
        for (size_t k = 0; k < shares.size(); ++k)
        {
            lowest = std::min(lowest, shares[k] * 1000.0);
            highest = std::max(highest, shares[k] * 1000.0);
            if (k >= 10 && k + 10 < shares.size())
                worstInner = std::max(worstInner, std::abs(shares[k] * 1000.0 - 1.0));
        }
        std::printf("      Anteil je Karte am Inlet: %.3f bis %.3f / N, innen hoechstens %.1f %% neben 1/N\n",
            lowest, highest, 100.0 * worstInner);
        Check(lowest > 0.5 && highest < 2.0 && worstInner < 0.05,
            "Anteil je Karte am Inlet: alle zwischen 1/(2N) und 2/N, innen auf 5 % genau 1/N");
    }

    // 10) Normalverteilung (100 um +- 25 um, Teil unter d = 0 hochgerechnet) mit den Startwerten.
    {
        const SCardSettings settings; // N 2000, Summe f = 0,5, gamma = 0,8
        const SSizeDistribution distribution{ EDistributionType::Normal, 100e-6, 25e-6 };
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6);
        std::printf("      kleinste Karte %.3g um, groesste %.4g um; Raender %zu / %zu Karten; schwerste Karte %.2f, "
            "meiste Partikel %.2f x Mittel\n", placement.sizes.front() * 1e6, placement.sizes.back() * 1e6,
            placement.fineCards, placement.coarseCards, placement.maxMassRatio, placement.maxNumberRatio);
        double sum = 0.0;
        for (double m : placement.massFractions)
            sum += m;
        Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.sizes.front() > 0.0,
            "Normalverteilung: 2000 Karten, Masse 1, alle Groessen > 0");
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

    // 12) Warnung bei zu wenigen Karten an den Raendern und bei ungueltiger Verteilung.
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
        const auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
        Check(warnings.size() == 1 && has(warnings[0], "An beiden Raendern") && has(warnings[0], "feinsten 5 % der Partikel")
            && has(warnings[0], "groebsten 5 % der Masse") && has(warnings[0], "mehr Karten je Zeitschritt")
            && !has(warnings[0], "f kleiner") && !has(warnings[0], "f groesser"),
            "20 Karten: Warnung fuer beide Raender, Abhilfe ohne f");

        // Nur ein Rand zu duenn: nur dieser wird genannt, und nur Abhilfen,
        // die dort helfen und noch moeglich sind (gamma = 0,3 ist das Kleinste).
        graph.GetInlet(inlet)->SetSizeDistribution({ EDistributionType::LogNormal, 100e-6, 1.0 });
        SCardSettings numberOnly;
        numberOnly.cardsPerSlice = 500;
        numberOnly.numberShare = 1.0;
        numberOnly.exponent = 0.3;
        graph.SetCardSettings(numberOnly);
        graph.RunSimulations(1);
        const std::vector<std::string> coarse = graph.GetSimulationWarnings();
        std::printf("      Hinweis: %s\n", coarse.empty() ? "-" : coarse[0].c_str());
        Check(coarse.size() == 1 && has(coarse[0], "Die groebsten 5 % der Masse bekommen nur") && !has(coarse[0], "feinsten")
            && has(coarse[0], "f kleiner") && !has(coarse[0], "gamma kleiner"),
            "f = 1, gamma = 0,3: nur der grobe Rand, Abhilfe f kleiner, nicht gamma kleiner");
        SCardSettings massOnly;
        massOnly.cardsPerSlice = 500;
        massOnly.numberShare = 0.0;
        massOnly.exponent = 1.0;
        graph.SetCardSettings(massOnly);
        graph.RunSimulations(1);
        const std::vector<std::string> fine = graph.GetSimulationWarnings();
        std::printf("      Hinweis: %s\n", fine.empty() ? "-" : fine[0].c_str());
        Check(fine.size() == 1 && has(fine[0], "Die feinsten 5 % der Partikel bekommen nur") && !has(fine[0], "groebsten")
            && has(fine[0], "f groesser") && has(fine[0], "gamma kleiner"),
            "f = 0, gamma = 1: nur der feine Rand, Abhilfe f groesser und gamma kleiner");
        graph.SetCardSettings(SCardSettings());
        graph.GetInlet(inlet)->SetSizeDistribution({ EDistributionType::LogNormal, 100e-6, 0.0 });
        graph.RunSimulations(1);
        const std::vector<std::string> invalid = graph.GetSimulationWarnings();
        Check(invalid.size() == 1 && invalid[0].find("Keine Karten erzeugt") != std::string::npos,
            "ungueltige Verteilung: Warnung keine Karten");
    }

    // 13) RRSB mit n = 2: die Anzahl waechst zu kleinen Groessen ohne Grenze -
    //     Partikel unter d_min = 1 um zaehlen wie bei d_min, Masse bleibt exakt.
    {
        const SCardSettings settings;
        const SSizeDistribution distribution{ EDistributionType::RRSB, 65e-6, 2.0 };
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6);
        double sum = 0.0;
        size_t belowGrid = 0;
        for (size_t k = 0; k < placement.sizes.size(); ++k)
        {
            sum += placement.massFractions[k];
            belowGrid += placement.sizes[k] < 1e-6 ? 1 : 0;
        }
        std::printf("      RRSB n = 2: kleinste Karte %.3g um, %zu Karten unter 1 um, Raender %zu / %zu Karten, "
            "meiste Partikel je Karte %.2f x Mittel\n", placement.sizes.front() * 1e6, belowGrid, placement.fineCards,
            placement.coarseCards, placement.maxNumberRatio);
        Check(placement.sizes.size() == 2000 && NearAbs(sum, 1.0, 1e-12) && placement.maxNumberRatio < 3.0,
            "RRSB n = 2: 2000 Karten, Masse 1, keine Karte mit mehr als 3-mal so vielen Partikeln wie im Mittel");
    }

    // 14) Verteilung aus den Karten (Kartenansicht): feine Klassen,
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
        const SCardPlacement placement = PlaceCardsAtQuantiles({ EDistributionType::RRSB, 65e-6, 2.0 }, settings, 1e-6);
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
