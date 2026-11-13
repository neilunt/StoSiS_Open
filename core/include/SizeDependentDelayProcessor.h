#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include "StreamProcessor.h"
#include "StreamMixing.h"
#include "Random.h"

namespace stosis
{
    // Erste konkrete Verarbeitungseinheit: verzoegert jede Groessenklasse
    // um eine groessenabhaengige Zeit
    //   delay(size) = delayRef * (size / referenceSize)^sizeExponent
    // Kein Zufall, keine Impulsantwort - der Eingangsverlauf kommt 1:1,
    // nur zeitlich verschoben, am Ausgang an.
    //
    // Hold-up: ein Ringpuffer mit den Eingaengen der letzten Schritte -
    // das ist das Material, das gerade "unterwegs" in der Einheit ist.
    // Er ist genau so lang, wie die groesste Verzoegerung es verlangt.
    // Jeder Platz merkt sich auch Temperatur und Druck seines Materials:
    // T und p wandern mit dem Material. Kommt Material aus mehreren
    // Zeitpunkten gleichzeitig an (verschiedene Groessen sind verschieden
    // lang unterwegs), mischt MixConditions es wie ein Merger.
    //
    // Particle mode (methods handbook ch. 7.3, 9.4): every card gets the
    // delay of its own size, D/dt = q + r with whole steps q and remainder
    // 0 <= r < 1. Of the inflow of a step, the share 1 - r leaves after q
    // steps and r after q + 1 (the inflow is spread over the step, as for
    // plug flow in the vessel). Cards are not split: an account method
    // sends each whole card either "early" (q) or "late" (q + 1). There are
    // two accounts for all cards; each card pays 1 - r into "early" and r
    // into "late", goes to the higher one, and that account is charged 1.
    // As the two accounts always sum to zero, one balance (the "late"
    // account) is enough. Cards wait in bins, one per step of departure;
    // each card keeps the temperature and pressure of its inflow step.
    class CSizeDependentDelayProcessor : public IStreamProcessor
    {
    public:
        std::string GetTypeName() const override
        {
            return "Verzoegerung: Groessenabhaengig";
        }

        std::vector<SParameterDescriptor> GetParameterDescriptors() const override
        {
            return {
                {"delayRef", "Verzoegerung bei Referenzgroesse [s]", 0.0, 100.0, 2.0},
                {"referenceSize", std::string("Referenzgroesse d_ref [") + kSizeDisplayUnit + "]", 1e-9, 1.0, 100e-6, kSizeDisplayFactor},
                {"sizeExponent", "Groessenexponent p", -5.0, 5.0, 1.0},
            };
        }

        void SetParameter(const std::string& id, double value) override
        {
            if (id == "delayRef") m_delayRef = value;
            else if (id == "referenceSize") m_referenceSize = value;
            else if (id == "sizeExponent") m_sizeExponent = value;
        }

        double GetParameterValue(const std::string& id) const override
        {
            if (id == "delayRef") return m_delayRef;
            if (id == "referenceSize") return m_referenceSize;
            if (id == "sizeExponent") return m_sizeExponent;
            return 0.0;
        }

        bool SupportsParticles() const override { return true; }

        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_dt = grid.GetDt();
            const size_t numS = grid.numSizeClasses;
            m_numCompounds = grid.GetNumCompounds();
            m_compounds = grid.compounds;
            m_materials = grid.materials;
            m_missingHeatCapacity.clear();
            m_particles = grid.representation == ERepresentation::Particles;
            m_numTimeSteps = grid.numTimeSteps;

            // Particle mode: empty bins, balanced accounts, own random streams
            // for the start of the walk and for the lots of equal cards.
            m_pending.clear();
            m_lateAccount = 0.0;
            m_random.Seed(MakeUnitSeed(unitSeed, "Verzoegerung"));
            m_lotRandom.Seed(MakeUnitSeed(unitSeed, "Verzoegerung-Losnummer"));

            m_sizeWeights = grid.GetSizeGrid().GetWeights();
            m_delays.resize(numS);
            double maxDelay = 0.0;
            for (size_t s = 0; s < numS; ++s)
            {
                m_delays[s] = DelayOf(grid.GetSizeAt(s));
                maxDelay = std::max(maxDelay, m_delays[s]);
            }

            // Puffer muss die Eingaenge von "jetzt" bis "jetzt - maxDelay"
            // (plus einen Nachbarpunkt zum Interpolieren) halten. Laenger als
            // die ganze Simulation muss er nie sein - noch aeltere Werte gibt
            // es nicht, dort ist der Ausgang 0. The particle mode needs no
            // ring buffer.
            m_history.clear();
            m_historyTemperature.clear();
            m_historyPressure.clear();
            m_slotWeights.clear();
            if (m_particles)
                return;
            size_t capacity = grid.numTimeSteps + 1;
            if (m_dt > 0.0 && maxDelay / m_dt < static_cast<double>(capacity))
                capacity = std::min(capacity, static_cast<size_t>(std::ceil(maxDelay / m_dt)) + 2);
            m_history.assign(capacity, std::vector<double>(m_numCompounds * numS, 0.0));
            m_historyTemperature.assign(capacity, kRoomTemperature);
            m_historyPressure.assign(capacity, kRoomPressure);
            m_slotWeights.assign(capacity * m_numCompounds, 0.0);
        }

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            if (outputs.empty())
                return;
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            CStreamSlice& out = outputs[0];
            m_parts.clear();
            if (m_particles)
            {
                if (out.HasCards())
                    StepCards(context.stepIndex, input, out);
            }
            else
                StepContinuum(context.stepIndex, input, out);

            // T und p am Ausgang: Mischung der Pufferplaetze, aus denen gerade
            // Material kommt. Kommt nichts an, gelten die Werte des Eingangs.
            if (m_parts.empty())
            {
                out.SetTemperature(input ? input->GetTemperature() : kRoomTemperature);
                out.SetPressure(input ? input->GetPressure() : kRoomPressure);
                return;
            }
            const SMixResult mix = MixConditions(m_parts, m_compounds, m_materials);
            out.SetTemperature(mix.temperature);
            out.SetPressure(mix.pressure);
            m_missingHeatCapacity.insert(mix.missingHeatCapacity.begin(), mix.missingHeatCapacity.end());
        }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            for (const std::string& name : m_missingHeatCapacity)
                warnings.push_back("Keine Waermekapazitaet fuer " + name +
                    " - Austrittstemperatur nur nach Masse gewichtet statt aus der Energiebilanz.");
            return warnings;
        }

        std::string DescribeForLog() const override
        {
            return m_particles ? "Karten ganz nach Konto-Verfahren frueh/spaet (je Karte eigene Verzoegerung)" : std::string();
        }

        // --- For tests and display ------------------------------------------

        // Mass inside the unit (particle mode) [kg]: all cards that have not
        // left yet, including those whose delay reaches beyond the end.
        double GetContentMass() const
        {
            double mass = 0.0;
            for (const auto& entry : m_pending)
                for (double m : entry.second.masses)
                    mass += m;
            return mass;
        }

        size_t GetContentCards() const
        {
            size_t cards = 0;
            for (const auto& entry : m_pending)
                cards += entry.second.sizes.size();
            return cards;
        }

        // Balance of the "late" account (the "early" account is its negative).
        double GetLateAccount() const { return m_lateAccount; }

    private:
        double DelayOf(double size) const
        {
            const double safeSize = std::max(size, 1e-12);
            return m_delayRef * std::pow(safeSize / m_referenceSize, m_sizeExponent);
        }

        // ---------------- Kontinuum ----------------

        void StepContinuum(size_t k, const CStreamSlice* input, CStreamSlice& out)
        {
            if (m_history.empty())
                return;
            const size_t capacity = m_history.size();
            const size_t numS = m_delays.size();

            // Aktuellen Eingang in den Puffer legen (unverbunden = 0).
            const size_t numC = m_numCompounds;
            const size_t current = k % capacity;
            std::vector<double>& slot = m_history[current];
            for (size_t c = 0; c < numC; ++c)
                for (size_t s = 0; s < numS; ++s)
                    slot[c * numS + s] = (input && c < input->GetNumCompounds() && s < input->GetNumSizeClasses())
                        ? input->GetValue(c, s) : 0.0;
            m_historyTemperature[current] = input ? input->GetTemperature() : kRoomTemperature;
            m_historyPressure[current] = input ? input->GetPressure() : kRoomPressure;

            // Wie viel Material jedes Pufferplatzes jetzt austritt (je Stoff,
            // Trapezgewichte des Rasters wie CStreamSlice::GetCompoundWeight) - fuer T und p.
            std::fill(m_slotWeights.begin(), m_slotWeights.end(), 0.0);

            const size_t numOutS = std::min(numS, out.GetNumSizeClasses());
            for (size_t s = 0; s < numOutS; ++s)
            {
                // Zeitpunkt im Eingangssignal, der jetzt am Ausgang ankommt.
                const double sourceTime = static_cast<double>(k) * m_dt - m_delays[s];
                if (sourceTime < 0.0 || m_dt <= 0.0)
                    continue; // noch nichts angekommen -> bleibt 0

                const double trapezoid = m_sizeWeights[s];
                const double sourceIndex = sourceTime / m_dt;
                const size_t i0 = static_cast<size_t>(sourceIndex);
                if (i0 >= k)
                {
                    for (size_t c = 0; c < numC; ++c)
                    {
                        const double value = m_history[current][c * numS + s];
                        out.SetValue(c, s, value);
                        m_slotWeights[current * numC + c] += trapezoid * value;
                    }
                    continue;
                }
                if (k - i0 >= capacity)
                    continue; // aelter als der Puffer (sollte nicht vorkommen)

                const double w = sourceIndex - static_cast<double>(i0);
                const size_t a = i0 % capacity;
                const size_t b = (i0 + 1) % capacity;
                for (size_t c = 0; c < numC; ++c)
                {
                    const double valueA = m_history[a][c * numS + s];
                    const double valueB = m_history[b][c * numS + s];
                    out.SetValue(c, s, (1.0 - w) * valueA + w * valueB);
                    m_slotWeights[a * numC + c] += trapezoid * (1.0 - w) * valueA;
                    m_slotWeights[b * numC + c] += trapezoid * w * valueB;
                }
            }

            // Pufferplaetze, aus denen gerade Material kommt, fuer T und p.
            for (size_t i = 0; i < capacity; ++i)
            {
                const auto first = m_slotWeights.begin() + static_cast<std::ptrdiff_t>(i * numC);
                if (std::none_of(first, first + static_cast<std::ptrdiff_t>(numC), [](double v) { return v > 0.0; }))
                    continue;
                SMixPart part;
                part.compoundWeights.assign(first, first + static_cast<std::ptrdiff_t>(numC));
                part.temperature = m_historyTemperature[i];
                part.pressure = m_historyPressure[i];
                m_parts.push_back(std::move(part));
            }
        }

        // ---------------- Particle mode ----------------

        // Cards waiting for one step of departure, with one mixing part
        // (temperature, pressure, mass per compound) per inflow step.
        struct SPendingBin
        {
            std::vector<double> sizes;
            std::vector<double> masses;     // [card * numCompounds + compound], kg
            std::vector<SMixPart> parts;
            std::vector<size_t> partSteps;  // inflow step of each part
        };

        void StepCards(size_t k, const CStreamSlice* input, CStreamSlice& out)
        {
            if (input && input->HasCards() && input->GetNumCards() > 0)
                AdmitCards(k, *input);

            // Release the bin of this step; it holds cards from many inflow
            // steps, because sizes differ in their delay.
            const auto due = m_pending.find(k);
            if (due == m_pending.end())
                return;
            SPendingBin& bin = due->second;
            out.SetCards(std::move(bin.sizes), std::move(bin.masses));
            m_parts = std::move(bin.parts);
            m_pending.erase(due);
        }

        // Orders the cards of the inflow by dominant compound and size, so
        // that every region of the distribution gets its share of late cards;
        // equal cards are shuffled first (as in the vessel, otherwise heavy
        // and light cards of the same size could always take the same way).
        // Then one pass from a random start through the walk of the accounts.
        void AdmitCards(size_t k, const CStreamSlice& input)
        {
            const size_t n = input.GetNumCards();
            const size_t numC = m_numCompounds;
            const size_t inC = input.GetNumCompounds();
            m_dominant.assign(n, 0);
            for (size_t i = 0; i < n; ++i)
            {
                size_t best = 0;
                for (size_t c = 1; c < inC; ++c)
                    if (input.GetCardMass(i, c) > input.GetCardMass(i, best))
                        best = c;
                m_dominant[i] = static_cast<std::uint32_t>(best);
            }
            m_order.resize(n);
            std::iota(m_order.begin(), m_order.end(), size_t(0));
            std::stable_sort(m_order.begin(), m_order.end(), [&](size_t a, size_t b)
                {
                    return m_dominant[a] != m_dominant[b] ? m_dominant[a] < m_dominant[b]
                        : input.GetCardSize(a) < input.GetCardSize(b);
                });
            for (size_t first = 0; first < n;)
            {
                size_t last = first + 1;
                while (last < n && m_dominant[m_order[last]] == m_dominant[m_order[first]]
                    && input.GetCardSize(m_order[last]) == input.GetCardSize(m_order[first]))
                    ++last;
                for (size_t i = last - 1; i > first; --i)
                {
                    const size_t span = i - first + 1;
                    const size_t j = first + std::min(static_cast<size_t>(m_lotRandom.Uniform() * static_cast<double>(span)), span - 1);
                    std::swap(m_order[i], m_order[j]);
                }
                first = last;
            }

            // Cards whose delay reaches beyond the last step stay inside; they
            // are kept in the bin "never" (the step after the last one) and
            // take no part in the walk.
            const size_t never = m_numTimeSteps;
            const double horizon = static_cast<double>(m_numTimeSteps);
            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(n)), n - 1);
            for (size_t j = 0; j < n; ++j)
            {
                const size_t card = m_order[start + j < n ? start + j : start + j - n];
                const double steps = m_dt > 0.0 ? DelayOf(input.GetCardSize(card)) / m_dt : 0.0;
                size_t departure = never;
                if (steps < horizon)
                {
                    const double whole = std::floor(steps);
                    const double rest = steps - whole;
                    // Two accounts "early" and "late" summing to zero: after
                    // paying in, "late" leads when it exceeds 1/2 (a tie goes
                    // to "early"), and the chosen account is charged one card.
                    m_lateAccount += rest;
                    const bool late = m_lateAccount > 0.5;
                    if (late)
                        m_lateAccount -= 1.0;
                    departure = std::min(k + static_cast<size_t>(whole) + (late ? 1 : 0), never);
                }
                SPendingBin& bin = m_pending[departure];
                if (bin.partSteps.empty() || bin.partSteps.back() != k)
                {
                    SMixPart part;
                    part.compoundWeights.assign(numC, 0.0);
                    part.temperature = input.GetTemperature();
                    part.pressure = input.GetPressure();
                    bin.parts.push_back(std::move(part));
                    bin.partSteps.push_back(k);
                }
                SMixPart& part = bin.parts.back();
                bin.sizes.push_back(input.GetCardSize(card));
                for (size_t c = 0; c < numC; ++c)
                {
                    const double mass = c < inC ? input.GetCardMass(card, c) : 0.0;
                    bin.masses.push_back(mass);
                    part.compoundWeights[c] += mass;
                }
            }
        }

        double m_delayRef = 2.0;
        double m_referenceSize = 100e-6; // m
        double m_sizeExponent = 1.0;

        double m_dt = 0.0;
        size_t m_numCompounds = 1;
        std::vector<std::string> m_compounds;
        const CMaterialDatabase* m_materials = nullptr;
        std::vector<double> m_delays;               // pro Groessenklasse
        std::vector<double> m_sizeWeights;          // Trapezgewichte des Groessenrasters
        std::vector<std::vector<double>> m_history; // Ringpuffer [Schritt % Kapazitaet][Stoff * numS + Klasse]
        std::vector<double> m_historyTemperature;   // je Pufferplatz [K]
        std::vector<double> m_historyPressure;      // je Pufferplatz [Pa]
        std::vector<double> m_slotWeights;          // [Pufferplatz * numC + Stoff], je Schritt neu
        std::vector<SMixPart> m_parts;
        std::set<std::string> m_missingHeatCapacity;

        // Particle mode
        bool m_particles = false;
        size_t m_numTimeSteps = 0;
        std::map<size_t, SPendingBin> m_pending;    // step of departure -> waiting cards
        double m_lateAccount = 0.0;                 // "late" account; "early" is its negative
        CRandom m_random;                           // start of the walk per step
        CRandom m_lotRandom;                        // lots among equal cards
        std::vector<size_t> m_order;
        std::vector<std::uint32_t> m_dominant;
    };
}
