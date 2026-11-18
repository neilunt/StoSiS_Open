// Compound separator and classifier (Methodenhandbuch 7.6, 7.7): settling
// velocity after Kaskas (force balance, Stokes limit, Newton range),
// continuum exactly T at every node and compound, particle mode with the
// account method per compound and size range, missing material data.
#include "CompoundSeparatorUnit.h"
#include "ClassifierUnit.h"
#include "CardPlacement.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

namespace
{
    // Cards of one distribution for one compound, scaled to mass [kg].
    void AddCards(CStreamSlice& slice, const SSizeDistribution& distribution, size_t numCards, size_t compound, double mass)
    {
        SCardSettings settings;
        settings.cardsPerSlice = numCards;
        const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6);
        for (size_t k = 0; k < placement.sizes.size(); ++k)
            slice.AddCard(placement.sizes[k], compound, mass * placement.massFractions[k]);
    }

    size_t DominantCompound(const CStreamSlice& slice, size_t card)
    {
        size_t best = 0;
        for (size_t c = 1; c < slice.GetNumCompounds(); ++c)
            if (slice.GetCardMass(card, c) > slice.GetCardMass(card, best))
                best = c;
        return best;
    }

    struct SParticleResult
    {
        bool countsOk = true;
        bool massOk = true;
        double share = 0.0;         // mass share in output 1 over all steps
        double expectedShare = 0.0; // sum of T * m / sum of m
        double worstRange = 0.0;    // largest deviation per compound and size range
        double worstAccount = 0.0;
        double largestCardShare = 0.0;
    };

    // Runs a separator for many steps on the same feed and compares output 1
    // per compound and size range with the expected share T of every card.
    SParticleResult RunParticles(CTwoWaySeparatorUnit& unit, const SSimulationGrid& grid, const CStreamSlice& input,
        const std::function<double(size_t card)>& expectedShare, size_t steps)
    {
        SParticleResult r;
        std::vector<double> sizes, masses;
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            sizes.push_back(input.GetCardSize(k));
            masses.push_back(input.GetCardMass(k));
        }
        const std::vector<double> shares = ComputeCardShares(sizes, masses, grid.cards, grid.GetCardCountMin());
        r.largestCardShare = *std::max_element(shares.begin(), shares.end());

        std::map<std::pair<size_t, int>, double> rangeIn, rangeExpected, rangeFirst;
        double expectedTotal = 0.0;
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            const std::pair<size_t, int> key{ DominantCompound(input, k), GetCardRangeIndex(sizes[k], grid.cards) };
            rangeIn[key] += masses[k];
            rangeExpected[key] += expectedShare(k) * masses[k];
            expectedTotal += expectedShare(k) * masses[k];
        }

        std::vector<CStreamSlice> outputs(2);
        double firstTotal = 0.0;
        for (size_t k = 0; k < steps; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(input.GetNumSizeClasses(), input.GetNumCompounds(), ERepresentation::Particles);
            unit.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            r.countsOk = r.countsOk && outputs[0].GetNumCards() + outputs[1].GetNumCards() == input.GetNumCards();
            r.massOk = r.massOk && NearRel(outputs[0].GetCardsMass() + outputs[1].GetCardsMass(), input.GetCardsMass(), 1e-12);
            firstTotal += outputs[0].GetCardsMass();
            r.worstAccount = std::max(r.worstAccount, unit.GetLargestAccount());
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                rangeFirst[{ DominantCompound(outputs[0], c), GetCardRangeIndex(outputs[0].GetCardSize(c), grid.cards) }]
                    += outputs[0].GetCardMass(c);
        }
        for (const auto& e : rangeIn)
        {
            if (e.second < 0.01 * input.GetCardsMass())
                continue;
            const double share = rangeFirst[e.first] / (e.second * static_cast<double>(steps));
            r.worstRange = std::max(r.worstRange, std::abs(share - rangeExpected[e.first] / e.second));
        }
        r.share = firstTotal / (input.GetCardsMass() * static_cast<double>(steps));
        r.expectedShare = expectedTotal / input.GetCardsMass();
        return r;
    }

    SClassifierSettings MakeClassifier(EScreenModel model, double cutVelocity, double alpha, const std::string& fluid)
    {
        SClassifierSettings s;
        s.model = model;
        s.cutVelocity = cutVelocity;
        s.alpha = alpha;
        s.fluid = fluid;
        return s;
    }
}

int main()
{
    CMaterialDatabase materials;
    std::string error;
    Check(materials.LoadFromFile(DataFile("Materialien.json"), error), "Materialien.json eingelesen");
    const double T = kRoomTemperature, p = kRoomPressure;
    const SClassifierFluid air = GetClassifierFluid(&materials, "Air", T, p);
    const SClassifierFluid water = GetClassifierFluid(&materials, "H2O", T, p);
    const double rhoSand = GetCompoundDensity(&materials, "Sand", T, p);
    const double rhoCoal = GetCompoundDensity(&materials, "Coal", T, p);
    std::printf("      Luft %.4g kg/m3, %.4g Pa s; Wasser %.4g kg/m3, %.4g Pa s; Sand %.4g, Coal %.4g kg/m3\n",
        air.density, air.viscosity, water.density, water.viscosity, rhoSand, rhoCoal);
    Check(air.ok && water.ok && rhoSand > 0.0 && rhoCoal > 0.0, "Stoffwerte: Luft, Wasser, Sand und Coal vorhanden");

    // 1) Settling velocity after Kaskas.
    {
        // a) Force balance: c_w(Re) of Kaskas equals 4 d (rho_p - rho_f) g / (3 rho_f w^2).
        double worstBalance = 0.0, largestRe = 0.0;
        bool rising = true;
        for (const SClassifierFluid* fluid : { &air, &water })
            for (double rho : { rhoCoal, rhoSand, 7870.0 })
            {
                double last = 0.0;
                for (int i = 0; i <= 500; ++i)
                {
                    const double d = 1e-7 * std::pow(10.0, 6.0 * i / 500.0); // 0.1 um to 100 mm
                    const SSettlingVelocity w = ComputeSettlingVelocity(d, rho, fluid->density, fluid->viscosity);
                    const double cw = 4.0 * d * (rho - fluid->density) * kStandardGravity / (3.0 * fluid->density * w.velocity * w.velocity);
                    worstBalance = std::max(worstBalance, std::abs(cw / KaskasDragCoefficient(w.reynolds) - 1.0));
                    const double re = w.velocity * d * fluid->density / fluid->viscosity;
                    worstBalance = std::max(worstBalance, std::abs(re / w.reynolds - 1.0));
                    largestRe = std::max(largestRe, w.reynolds);
                    rising = rising && w.velocity > last;
                    last = w.velocity;
                }
            }
        std::printf("      Kraeftegleichgewicht: groesste relative Abweichung %.1e (Re bis %.2g)\n", worstBalance, largestRe);
        Check(worstBalance < 1e-12, "Kaskas: Gewicht - Auftrieb = Widerstand mit c_w = 24/Re + 4/sqrt(Re) + 0,4 (0,1 um bis 100 mm)");
        Check(rising, "Kaskas: w_s steigt mit der Groesse (Luft und Wasser, drei Dichten)");

        // b) Stokes limit: the term 4/sqrt(Re) lowers w_s against Stokes by
        //    about sqrt(Re)/6, which vanishes for small particles.
        bool stokesOk = true;
        for (double d : { 1e-6, 0.1e-6 })
        {
            const double stokes = (rhoSand - air.density) * kStandardGravity * d * d / (18.0 * air.viscosity);
            const SSettlingVelocity w = ComputeSettlingVelocity(d, rhoSand, air.density, air.viscosity);
            const double deviation = 1.0 - w.velocity / stokes;
            std::printf("      %g um Sand in Luft: w_s = %.6e m/s, Stokes %.6e m/s, Re = %.1e, Abweichung %.2e (sqrt(Re)/6 = %.2e)\n",
                d * 1e6, w.velocity, stokes, w.reynolds, deviation, std::sqrt(w.reynolds) / 6.0);
            stokesOk = stokesOk && deviation > 0.0 && NearRel(deviation, std::sqrt(w.reynolds) / 6.0, 0.01);
        }
        Check(stokesOk, "Kaskas: Stokes-Grenzfall, Abweichung wie sqrt(Re)/6 (1 um: 3e-4, 0,1 um: 1e-5)");

        // c) Newton range: 20 mm sand in air, c_w close to 0.4.
        const SSettlingVelocity big = ComputeSettlingVelocity(20e-3, rhoSand, air.density, air.viscosity);
        std::printf("      20 mm Sand in Luft: w_s = %.4g m/s, Re = %.3g, c_w = %.4f\n", big.velocity, big.reynolds,
            KaskasDragCoefficient(big.reynolds));
        Check(big.reynolds > 1e4 && KaskasDragCoefficient(big.reynolds) < 0.45, "Kaskas: grobe Koerner im Newton-Bereich (c_w nahe 0,4)");

        // d) No settling if the particle is not heavier than the fluid; then T = 0.
        Check(ComputeSettlingVelocity(1e-3, 900.0, water.density, water.viscosity).velocity == 0.0
            && ComputeSettlingVelocity(1e-3, water.density, water.density, water.viscosity).velocity == 0.0
            && MakeClassifier(EScreenModel::Plitt, 0.1, 4.0, "H2O").GradeEfficiency(0.0) == 0.0,
            "Kaskas: leichter als das Fluid -> w_s = 0, alles ins Leichtgut");
    }

    // 2) Settings.
    {
        CClassifierUnit classifier;
        CCompoundSeparatorUnit separator;
        SCompoundSeparatorSettings tooMuch, twice, unnamed, fine;
        tooMuch.shares = { { "Fe", 1.2 } };
        twice.shares = { { "Fe", 0.9 }, { "Fe", 0.8 } };
        unnamed.shares = { { "", 0.5 } };
        fine.shares = { { "Fe", 1.0 }, { "Sand", 0.0 } };
        Check(!classifier.SetSettings(MakeClassifier(EScreenModel::Plitt, 0.0, 4.0, "Air"))
            && !classifier.SetSettings(MakeClassifier(EScreenModel::Plitt, 1.0, 4.0, ""))
            && !classifier.SetSettings(MakeClassifier(EScreenModel::MolerusHoffmann, 1.0, 0.0, "Air"))
            && classifier.SetSettings(MakeClassifier(EScreenModel::Plitt, 1.0, 0.0, "Air")),
            "Einstellungen Sichter: w_cut = 0, leeres Fluid, Molerus-Hoffmann mit alpha = 0 abgelehnt");
        Check(!separator.SetSettings(tooMuch) && !separator.SetSettings(twice) && !separator.SetSettings(unnamed)
            && separator.SetSettings(fine) && separator.GetSettings().ShareOf("Fe") == 1.0
            && separator.GetSettings().ShareOf("Coal") == 0.0,
            "Einstellungen Stofftrenner: Anteil > 1, doppelter und leerer Stoff abgelehnt; fehlender Stoff = 0");
    }

    // 3) Classifier, continuum: Schwergut = T(w_s(d, rho_c)) x Zulauf at every
    //    node and compound; sand before coal at every size.
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 150;
        grid.sizeMin = 1e-6;
        grid.sizeMax = 1e-2;
        grid.compounds = { "Sand", "Coal" };
        grid.materials = &materials;
        for (const char* fluidName : { "Air", "H2O" })
        {
            const SClassifierFluid& fluid = std::string(fluidName) == "Air" ? air : water;
            CClassifierUnit classifier;
            classifier.SetSettings(MakeClassifier(EScreenModel::MolerusHoffmann, std::string(fluidName) == "Air" ? 1.0 : 0.01, 3.0, fluidName));
            classifier.Initialize(grid, 1);
            CStreamSlice input;
            input.Resize(grid.numSizeClasses, 2);
            for (size_t c = 0; c < 2; ++c)
                for (size_t s = 0; s < grid.numSizeClasses; ++s)
                    input.SetValue(c, s, (1.0 + c) * std::exp(-0.01 * static_cast<double>(s)));
            std::vector<CStreamSlice> outputs(2);
            classifier.Step({ 0, 0.0, 0.1 }, { &input }, outputs);
            double worstShare = 0.0, worstSum = 0.0;
            bool sandFirst = true;
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
            {
                double t[2] = { 0.0, 0.0 };
                for (size_t c = 0; c < 2; ++c)
                {
                    const double v = input.GetValue(c, s);
                    const double w = ComputeSettlingVelocity(grid.GetSizeAt(s), c == 0 ? rhoSand : rhoCoal, fluid.density, fluid.viscosity).velocity;
                    t[c] = classifier.GetSettings().GradeEfficiency(w);
                    worstShare = std::max(worstShare, std::abs(outputs[0].GetValue(c, s) - t[c] * v));
                    worstSum = std::max(worstSum, std::abs(outputs[0].GetValue(c, s) + outputs[1].GetValue(c, s) - v));
                }
                sandFirst = sandFirst && t[0] >= t[1];
            }
            const std::string where = std::string(" (") + fluidName + ")";
            Check(worstShare == 0.0, "Sichter Kontinuum: Schwergut = T(w_s) x Zulauf an jedem Stuetzpunkt, jeder Stoff" + where);
            Check(worstSum <= 1e-15, "Sichter Kontinuum: Schwergut + Leichtgut = Zulauf" + where);
            Check(sandFirst, "Sichter Kontinuum: Sand (schwerer) geht bei jeder Groesse mindestens so stark ins Schwergut wie Coal" + where);
            Check(classifier.GetWarnings().empty(), "Sichter Kontinuum: keine Hinweise bei vollstaendigen Stoffwerten" + where);
        }
    }

    // 4) Classifier, particle mode: air classifier around 100 um sand / 150 um
    //    coal; per compound and size range the heavy product gets T(w_s).
    {
        SSimulationGrid grid;
        grid.representation = ERepresentation::Particles;
        grid.compounds = { "Sand", "Coal" };
        grid.materials = &materials;
        CClassifierUnit classifier;
        classifier.SetSettings(MakeClassifier(EScreenModel::Plitt, 0.3, 3.0, "Air"));
        classifier.Initialize(grid, 42);
        CStreamSlice input;
        input.Resize(300, 2, ERepresentation::Particles);
        AddCards(input, SSizeDistribution{ EDistributionType::LogNormal, 80e-6, 0.6 }, 2000, 0, 3.0);
        AddCards(input, SSizeDistribution{ EDistributionType::RRSB, 150e-6, 3.0 }, 2000, 1, 1.0);
        const auto expected = [&](size_t card)
        {
            const double rho = DominantCompound(input, card) == 0 ? rhoSand : rhoCoal;
            return classifier.GetSettings().GradeEfficiency(
                ComputeSettlingVelocity(input.GetCardSize(card), rho, air.density, air.viscosity).velocity);
        };
        const SParticleResult r = RunParticles(classifier, grid, input, expected, 1000);
        std::printf("      Schwergut %.5f (erwartet %.5f); je Stoff und Groessenbereich hoechstens %.1e neben T;\n"
                    "      groesstes Konto %.2e, groesster Kartenanteil %.2e\n",
            r.share, r.expectedShare, r.worstRange, r.worstAccount, r.largestCardShare);
        Check(r.countsOk, "Sichter Partikel: Schwergut + Leichtgut = Zulauf an Karten (nichts kopiert)");
        Check(r.massOk, "Sichter Partikel: Masse bleibt in jedem Schritt");
        Check(r.expectedShare > 0.1 && r.expectedShare < 0.9 && NearAbs(r.share, r.expectedShare, 1e-3),
            "Sichter Partikel: Schwergut ueber 1000 Schritte wie im Kontinuum (1e-3)");
        Check(r.worstRange < 5e-3, "Sichter Partikel: jeder Stoff und Groessenbereich geht mit T(w_s) ins Schwergut (5e-3)");
        Check(r.worstAccount <= 0.5 * r.largestCardShare + 1e-15, "Sichter Partikel: Konten bleiben unter einem halben Kartenanteil");
        Check(classifier.GetWarnings().empty(), "Sichter Partikel: keine Hinweise (Re klein, Stoffwerte vollstaendig)");
    }

    // 5) Classifier with missing data: unknown compound and unknown fluid go
    //    completely to the light product, with a hint each; coarse cards
    //    beyond Re = 2e5 give a hint.
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 50;
        grid.compounds = { "Sand", "Beton" };
        grid.materials = &materials;
        CClassifierUnit classifier;
        classifier.SetSettings(MakeClassifier(EScreenModel::Plitt, 0.3, 3.0, "Air"));
        classifier.Initialize(grid, 1);
        CStreamSlice input;
        input.Resize(grid.numSizeClasses, 2);
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                input.SetValue(c, s, 1.0);
        std::vector<CStreamSlice> outputs(2);
        classifier.Step({ 0, 0.0, 0.1 }, { &input }, outputs);
        double betonHeavy = 0.0, sandHeavy = 0.0;
        for (size_t s = 0; s < grid.numSizeClasses; ++s)
        {
            sandHeavy += outputs[0].GetValue(0, s);
            betonHeavy += outputs[0].GetValue(1, s);
        }
        const std::vector<std::string> hints = classifier.GetWarnings();
        Check(sandHeavy > 0.0 && betonHeavy == 0.0 && hints.size() == 1 && hints[0].find("Beton") != std::string::npos,
            "Sichter: Stoff ohne Dichte ganz ins Leichtgut, ein Hinweis");

        CClassifierUnit noFluid;
        noFluid.SetSettings(MakeClassifier(EScreenModel::Plitt, 0.3, 3.0, "Helium"));
        noFluid.Initialize(grid, 1);
        noFluid.Step({ 0, 0.0, 0.1 }, { &input }, outputs);
        double heavy = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                heavy += outputs[0].GetValue(c, s);
        const std::vector<std::string> fluidHints = noFluid.GetWarnings();
        Check(heavy == 0.0 && !fluidHints.empty() && fluidHints[0].find("Helium") != std::string::npos,
            "Sichter: unbekanntes Fluid -> alles ins Leichtgut, mit Hinweis");

        SSimulationGrid coarse;
        coarse.representation = ERepresentation::Particles;
        coarse.compounds = { "Sand" };
        coarse.materials = &materials;
        CClassifierUnit big;
        big.SetSettings(MakeClassifier(EScreenModel::Plitt, 10.0, 3.0, "Air"));
        big.Initialize(coarse, 1);
        CStreamSlice cards;
        cards.Resize(300, 1, ERepresentation::Particles);
        cards.AddCard(5e-3, 0, 1.0);
        cards.AddCard(200e-3, 0, 1.0);
        for (CStreamSlice& o : outputs)
            o.Resize(300, 1, ERepresentation::Particles);
        big.Step({ 0, 0.0, 0.1 }, { &cards }, outputs);
        const std::vector<std::string> reHints = big.GetWarnings();
        if (!reHints.empty())
            std::printf("      Hinweis: %s\n", reHints[0].c_str());
        Check(reHints.size() == 1 && reHints[0].find("Re = 2e5") != std::string::npos,
            "Sichter: Karte jenseits von Re = 2e5 gibt einen Hinweis");
    }

    // 6) Compound separator, continuum: abgetrennt = f_c x Zulauf, the size
    //    distribution of every compound is the same in both outputs.
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 100;
        grid.compounds = { "Fe", "Sand", "Coal" };
        CCompoundSeparatorUnit separator;
        SCompoundSeparatorSettings settings;
        settings.shares = { { "Fe", 0.95 }, { "Coal", 0.1 }, { "Glas", 0.5 } };
        separator.SetSettings(settings);
        separator.Initialize(grid, 1);
        CStreamSlice input;
        input.Resize(grid.numSizeClasses, 3);
        for (size_t c = 0; c < 3; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                input.SetValue(c, s, (1.0 + c) * std::exp(-0.02 * static_cast<double>(s)));
        input.SetTemperature(330.0);
        std::vector<CStreamSlice> outputs(2);
        separator.Step({ 0, 0.0, 0.1 }, { &input }, outputs);
        const double f[3] = { 0.95, 0.0, 0.1 };
        double worstShare = 0.0, worstSum = 0.0;
        for (size_t c = 0; c < 3; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
            {
                const double v = input.GetValue(c, s);
                worstShare = std::max(worstShare, std::abs(outputs[0].GetValue(c, s) - f[c] * v));
                worstSum = std::max(worstSum, std::abs(outputs[0].GetValue(c, s) + outputs[1].GetValue(c, s) - v));
            }
        Check(worstShare == 0.0, "Stofftrenner Kontinuum: abgetrennt = Anteil des Stoffs x Zulauf, unabhaengig von der Groesse");
        Check(worstSum <= 1e-15, "Stofftrenner Kontinuum: abgetrennt + Rest = Zulauf");
        Check(outputs[0].GetTemperature() == 330.0 && outputs[1].GetTemperature() == 330.0,
            "Stofftrenner Kontinuum: Temperatur in beiden Ausgaengen wie im Zulauf");
        const std::vector<std::string> hints = separator.GetWarnings();
        Check(hints.size() == 1 && hints[0].find("Glas") != std::string::npos,
            "Stofftrenner: Hinweis auf einen Stoff, der im Flowsheet nicht vorkommt");
    }

    // 7) Compound separator, particle mode: magnet with Fe 0.95, sand 0 and
    //    coal 0.3; a card of iron and sand (1:1) goes with the mass mean.
    {
        SSimulationGrid grid;
        grid.representation = ERepresentation::Particles;
        grid.compounds = { "Fe", "Sand", "Coal" };
        CCompoundSeparatorUnit separator;
        SCompoundSeparatorSettings settings;
        settings.shares = { { "Fe", 0.95 }, { "Coal", 0.3 } };
        separator.SetSettings(settings);
        separator.Initialize(grid, 7);
        CStreamSlice input;
        input.Resize(300, 3, ERepresentation::Particles);
        AddCards(input, SSizeDistribution{ EDistributionType::LogNormal, 100e-6, 0.5 }, 1000, 0, 1.0);
        AddCards(input, SSizeDistribution{ EDistributionType::LogNormal, 120e-6, 0.4 }, 1000, 1, 4.0);
        AddCards(input, SSizeDistribution{ EDistributionType::RRSB, 200e-6, 3.0 }, 1000, 2, 2.0);
        const double f[3] = { 0.95, 0.0, 0.3 };
        const SParticleResult r = RunParticles(separator, grid, input,
            [&](size_t card) { return f[DominantCompound(input, card)]; }, 1000);
        std::printf("      abgetrennt %.5f (erwartet %.5f); je Stoff und Groessenbereich hoechstens %.1e neben f;\n"
                    "      groesstes Konto %.2e, groesster Kartenanteil %.2e\n",
            r.share, r.expectedShare, r.worstRange, r.worstAccount, r.largestCardShare);
        Check(r.countsOk && r.massOk, "Stofftrenner Partikel: Karten und Masse bleiben (nichts kopiert)");
        Check(NearAbs(r.share, r.expectedShare, 1e-3), "Stofftrenner Partikel: abgetrennt ueber 1000 Schritte wie im Kontinuum (1e-3)");
        Check(r.worstRange < 5e-3, "Stofftrenner Partikel: jeder Stoff und Groessenbereich mit seinem Anteil (5e-3)");
        Check(r.worstAccount <= 0.5 * r.largestCardShare + 1e-15, "Stofftrenner Partikel: Konten bleiben unter einem halben Kartenanteil");

        // A mixed card alone: half iron, half sand -> share (0.95 + 0) / 2 = 0.475.
        CCompoundSeparatorUnit mixed;
        mixed.SetSettings(settings);
        mixed.Initialize(grid, 9);
        CStreamSlice one;
        one.Resize(300, 3, ERepresentation::Particles);
        one.AddCard(100e-6, std::vector<double>{ 1.0, 1.0, 0.0 });
        std::vector<CStreamSlice> outputs(2);
        size_t first = 0;
        for (size_t k = 0; k < 1000; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 3, ERepresentation::Particles);
            mixed.Step({ k, 0.1 * k, 0.1 }, { &one }, outputs);
            first += outputs[0].GetNumCards();
        }
        std::printf("      Karte aus Fe und Sand (1:1): %zu von 1000 Schritten in Ausgang 1\n", first);
        Check(first >= 474 && first <= 476, "Stofftrenner Partikel: Karte aus Fe und Sand (1:1) geht mit dem Massenmittel 0,475 in Ausgang 1");
    }

    return Summary();
}
