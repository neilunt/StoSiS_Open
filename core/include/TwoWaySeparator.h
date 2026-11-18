#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <utility>
#include "FlowsheetUnit.h"
#include "Random.h"
#include "CardDensity.h"

namespace stosis
{
    // Common part of all separators with one input and two outputs that send
    // a share T of the feed to output 1 and the rest to output 2: screen
    // (T by size), compound separator (T by compound) and classifier (T by
    // settling velocity, i.e. size and density). No hold-up; mass is
    // conserved in every step. A derived unit only says what T is.
    //
    // Continuum: output 1 = T(compound, d) * feed, output 2 = the rest, at
    // every node of the size grid and for every compound (NodeShare).
    //
    // Particle mode: whole cards with the account method of the splitter
    // (Methodenhandbuch 9.4), but every card brings its own share T
    // (CardShare). Cards with T = 0 or T = 1 go straight to output 2 or 1.
    // The others are ordered by compound and size and walked once from a
    // random start; each pays T * s to the account of output 1 and
    // (1 - T) * s to the account of output 2 of its compound and size range
    // (factor 2, as the ranges of the card view) and goes completely to the
    // output with the higher balance, which is then charged s. s is the
    // card's share of the card density of the slice (ComputeCardShares).
    // Within a range T changes little, so there every (1/T)-th card goes to
    // output 1; a balance never moves a card into another range or compound.
    // The accounts carry on over the steps; cards are not copied.
    class CTwoWaySeparatorUnit : public IFlowsheetUnit
    {
    public:
        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_random.Seed(MakeUnitSeed(unitSeed, "Konto-Verfahren"));
            m_accounts.clear();
            m_particles = grid.representation == ERepresentation::Particles;
            m_cardSettings = grid.cards;
            m_countMin = grid.GetCardCountMin();
            InitializeShares(grid);
        }

        void Step(const SStepContext& /*context*/, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            if (!input || outputs.size() < 2)
                return; // outputs stay 0
            PrepareStep(*input);
            if (input->HasCards())
            {
                DistributeCards(*input, outputs);
                return;
            }
            CStreamSlice& first = outputs[0];
            CStreamSlice& second = outputs[1];
            first.AssignScaled(*input, 0.0);
            second.AssignScaled(*input, 0.0);
            const size_t numS = input->GetNumSizeClasses();
            for (size_t c = 0; c < input->GetNumCompounds(); ++c)
                for (size_t s = 0; s < numS; ++s)
                {
                    const double value = input->GetValue(c, s);
                    const double toFirst = value != 0.0 ? NodeShare(c, s) * value : 0.0;
                    first.SetValue(c, s, toFirst);
                    second.SetValue(c, s, value - toFirst);
                }
        }

        bool SupportsParticles() const override { return true; }

        // Largest balance of an account of output 1 (card shares) over all
        // compounds and size ranges. For tests: stays below about one card share.
        double GetLargestAccount() const
        {
            double largest = 0.0;
            for (const auto& entry : m_accounts)
                largest = std::max(largest, std::abs(entry.second));
            return largest;
        }

    protected:
        // Before every simulation, after the account state is reset.
        virtual void InitializeShares(const SSimulationGrid& grid) = 0;
        // Before every step, e.g. to follow temperature and pressure of the feed.
        virtual void PrepareStep(const CStreamSlice& /*input*/) {}
        // Share of compound c at node s of the size grid that goes to output 1 (continuum).
        virtual double NodeShare(size_t compound, size_t sizeIdx) const = 0;
        // Share of a card that goes to output 1, between 0 and 1 (particle mode).
        virtual double CardShare(const CStreamSlice& input, size_t card) const = 0;

        bool IsParticleMode() const { return m_particles; }

        // Index of the only compound of a card with mass; -1 if the card has
        // several compounds or none (then the derived unit averages).
        static int SingleCompound(const CStreamSlice& input, size_t card)
        {
            int found = -1;
            for (size_t c = 0; c < input.GetNumCompounds(); ++c)
                if (input.GetCardMass(card, c) != 0.0)
                {
                    if (found >= 0)
                        return -1;
                    found = static_cast<int>(c);
                }
            return found;
        }

    private:
        void DistributeCards(const CStreamSlice& input, std::vector<CStreamSlice>& outputs)
        {
            for (size_t i = 0; i < 2; ++i)
            {
                outputs[i].SetTemperature(input.GetTemperature());
                outputs[i].SetPressure(input.GetPressure());
            }
            const size_t numCards = input.GetNumCards();
            if (numCards == 0)
                return;

            // Order by dominant compound, then by size (as the splitter).
            const auto dominantCompound = [&input](size_t card)
            {
                size_t best = 0;
                for (size_t c = 1; c < input.GetNumCompounds(); ++c)
                    if (input.GetCardMass(card, c) > input.GetCardMass(card, best))
                        best = c;
                return best;
            };
            m_order.resize(numCards);
            std::iota(m_order.begin(), m_order.end(), size_t(0));
            std::stable_sort(m_order.begin(), m_order.end(), [&](size_t a, size_t b)
                {
                    const size_t ca = dominantCompound(a);
                    const size_t cb = dominantCompound(b);
                    return ca != cb ? ca < cb : input.GetCardSize(a) < input.GetCardSize(b);
                });

            m_sizes.resize(numCards);
            m_masses.resize(numCards);
            for (size_t k = 0; k < numCards; ++k)
            {
                m_sizes[k] = input.GetCardSize(k);
                m_masses[k] = input.GetCardMass(k);
            }
            std::vector<double> shares = ComputeCardShares(m_sizes, m_masses, m_cardSettings, m_countMin);
            if (!(std::accumulate(shares.begin(), shares.end(), 0.0) > 0.0))
                shares.assign(numCards, 1.0 / static_cast<double>(numCards));

            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(numCards)), numCards - 1);
            for (size_t j = 0; j < numCards; ++j)
            {
                const size_t card = m_order[(start + j) % numCards];
                const double t = CardShare(input, card);
                if (t <= 0.0 || t >= 1.0)
                {
                    outputs[t >= 1.0 ? 0 : 1].AppendCard(input, card);
                    continue;
                }
                // Balance a of output 1 for this compound and range; the balance of output 2 is -a.
                const double share = shares[card];
                double& a = m_accounts[{ dominantCompound(card), GetCardRangeIndex(m_sizes[card], m_cardSettings) }];
                a += t * share;
                const bool toFirst = a >= -a + share; // output 1: a + t s against output 2: -a + (1 - t) s
                if (toFirst)
                    a -= share;
                outputs[toFirst ? 0 : 1].AppendCard(input, card);
            }
        }

        CRandom m_random;                // own stream: start position per step
        std::map<std::pair<size_t, int>, double> m_accounts; // balance of output 1 per (compound, size range)
        std::vector<size_t> m_order;
        std::vector<double> m_sizes, m_masses;
        SCardSettings m_cardSettings;
        double m_countMin = 0.0;
        bool m_particles = false;
    };
}
