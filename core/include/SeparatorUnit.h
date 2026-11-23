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
    // Common part of all separators with one input and n >= 2 outputs that
    // send a share t_j of the feed to output j (sum of the shares = 1):
    // screen with one or more decks (shares by size), and the two-way
    // separators of TwoWaySeparator.h. No hold-up; mass is conserved in every
    // step. A derived unit only says what the shares are.
    //
    // Continuum: output j = t_j(compound, d) * feed at every node of the size
    // grid and for every compound (NodeShares); the last output gets the rest,
    // so the mass is conserved exactly.
    //
    // Particle mode: whole cards with the account method of the splitter
    // (Methodenhandbuch 9.4), but every card brings its own shares t_j
    // (CardShares). A card with only one output of share > 0 goes straight
    // there. The others are ordered by compound and size and walked once from
    // a random start; each pays t_j * s to the account of output j of its
    // compound and size range (factor 2, as the ranges of the card view) and
    // goes completely to the output with the highest balance among those with
    // t_j > 0, which is then charged s. s is the card's share of the card
    // density of the slice: without flattening (ComputeAccountShares) or with
    // it (ComputeCardShares), as the derived unit chooses. Within a range the
    // shares change little, so there every output gets its share of the
    // cards; a balance never moves a card into another range or compound. The
    // accounts carry on over the steps; cards are not copied.
    //
    // The balances of a range sum to 0; only the first n - 1 are stored, the
    // last is minus their sum. With two outputs this is the balance a of
    // output 1 and -a of output 2.
    class CSeparatorUnit : public IFlowsheetUnit
    {
    public:
        CSeparatorUnit(size_t numOutputs, bool flattenedAccount)
            : m_numOutputs(std::max<size_t>(numOutputs, 2)), m_flattenedAccount(flattenedAccount)
        {
        }

        size_t GetNumOutputs() const { return m_numOutputs; }

        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_random.Seed(MakeUnitSeed(unitSeed, "Konto-Verfahren"));
            m_accounts.clear();
            m_particles = grid.representation == ERepresentation::Particles;
            m_cardSettings = grid.cards;
            m_countMin = grid.GetCardCountMin();
            m_shares.assign(m_numOutputs, 0.0);
            InitializeShares(grid);
        }

        void Step(const SStepContext& /*context*/, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            if (!input || outputs.size() < m_numOutputs)
                return; // outputs stay 0
            PrepareStep(*input);
            if (input->HasCards())
            {
                DistributeCards(*input, outputs);
                return;
            }
            for (size_t j = 0; j < m_numOutputs; ++j)
                outputs[j].AssignScaled(*input, 0.0);
            const size_t last = m_numOutputs - 1;
            const size_t numS = input->GetNumSizeClasses();
            for (size_t c = 0; c < input->GetNumCompounds(); ++c)
                for (size_t s = 0; s < numS; ++s)
                {
                    const double value = input->GetValue(c, s);
                    double rest = value;
                    if (value != 0.0)
                    {
                        NodeShares(c, s, m_shares);
                        for (size_t j = 0; j < last; ++j)
                        {
                            const double part = m_shares[j] * value;
                            outputs[j].SetValue(c, s, part);
                            rest -= part;
                        }
                    }
                    else
                    {
                        for (size_t j = 0; j < last; ++j)
                            outputs[j].SetValue(c, s, 0.0);
                    }
                    outputs[last].SetValue(c, s, rest);
                }
        }

        bool SupportsParticles() const override { return true; }

        // Largest balance of an account (card shares) over all outputs,
        // compounds and size ranges. For tests: stays below about one card share.
        double GetLargestAccount() const
        {
            double largest = 0.0;
            for (const auto& entry : m_accounts)
            {
                double sum = 0.0;
                for (double balance : entry.second)
                {
                    largest = std::max(largest, std::abs(balance));
                    sum += balance;
                }
                largest = std::max(largest, std::abs(sum));
            }
            return largest;
        }

    protected:
        // Before every simulation, after the account state is reset.
        virtual void InitializeShares(const SSimulationGrid& grid) = 0;
        // Before every step, e.g. to follow temperature and pressure of the feed.
        virtual void PrepareStep(const CStreamSlice& /*input*/) {}
        // Shares of compound c at node s of the size grid for all outputs
        // (continuum); shares has GetNumOutputs() entries, sum 1.
        virtual void NodeShares(size_t compound, size_t sizeIdx, std::vector<double>& shares) const = 0;
        // Shares of a card for all outputs, each between 0 and 1, sum 1 (particle mode).
        virtual void CardShares(const CStreamSlice& input, size_t card, std::vector<double>& shares) const = 0;

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
            for (size_t i = 0; i < m_numOutputs; ++i)
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
            std::vector<double> shares = m_flattenedAccount
                ? ComputeCardShares(m_sizes, m_masses, m_cardSettings, m_countMin)
                : ComputeAccountShares(m_sizes, m_masses, m_cardSettings, m_countMin);
            if (!(std::accumulate(shares.begin(), shares.end(), 0.0) > 0.0))
                shares.assign(numCards, 1.0 / static_cast<double>(numCards));

            const size_t last = m_numOutputs - 1;
            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(numCards)), numCards - 1);
            for (size_t j = 0; j < numCards; ++j)
            {
                const size_t card = m_order[(start + j) % numCards];
                CardShares(input, card, m_shares);
                size_t candidates = 0;
                size_t only = last; // no output with share > 0: the last one
                for (size_t o = 0; o < m_numOutputs; ++o)
                    if (m_shares[o] > 0.0)
                    {
                        ++candidates;
                        only = o;
                    }
                if (candidates <= 1)
                {
                    outputs[only].AppendCard(input, card);
                    continue;
                }
                const double share = shares[card];
                std::vector<double>& balances = m_accounts[{ dominantCompound(card), GetCardRangeIndex(m_sizes[card], m_cardSettings) }];
                if (balances.empty())
                    balances.assign(last, 0.0);
                double sum = 0.0;
                for (size_t o = 0; o < last; ++o)
                {
                    balances[o] += m_shares[o] * share;
                    sum += balances[o];
                }
                const double lastBalance = -sum + share; // all payments together are s
                size_t best = m_numOutputs;
                double bestBalance = 0.0;
                for (size_t o = 0; o < last; ++o)
                    if (m_shares[o] > 0.0 && (best == m_numOutputs || balances[o] > bestBalance))
                    {
                        best = o;
                        bestBalance = balances[o];
                    }
                if (m_shares[last] > 0.0 && (best == m_numOutputs || lastBalance > bestBalance))
                    best = last;
                if (best < last)
                    balances[best] -= share; // the last balance follows from the sum
                outputs[best].AppendCard(input, card);
            }
        }

        size_t m_numOutputs = 2;
        bool m_flattenedAccount = true;
        CRandom m_random;                // own stream: start position per step
        std::map<std::pair<size_t, int>, std::vector<double>> m_accounts; // balances of outputs 1 ... n-1 per (compound, size range)
        std::vector<size_t> m_order;
        std::vector<double> m_sizes, m_masses;
        std::vector<double> m_shares;    // shares of the current node or card
        SCardSettings m_cardSettings;
        double m_countMin = 0.0;
        bool m_particles = false;
    };
}
