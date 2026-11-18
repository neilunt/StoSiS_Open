#pragma once
#include <vector>
#include <string>
#include <utility>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "TwoWaySeparator.h"

namespace stosis
{
    struct SCompoundSeparatorSettings
    {
        // Share of each compound that goes to output 1 ("abgetrennt"), by the
        // compound's name in the material file. Compounds not listed go
        // completely to output 2 ("Rest").
        std::vector<std::pair<std::string, double>> shares;

        // Empty if valid, otherwise one sentence per problem.
        std::string Check() const
        {
            std::string text;
            for (size_t i = 0; i < shares.size(); ++i)
            {
                const std::string& name = shares[i].first;
                if (name.empty())
                    text += "Stoffname fehlt. ";
                else if (!(shares[i].second >= 0.0 && shares[i].second <= 1.0))
                    text += "Anteil von " + name + " muss zwischen 0 und 1 liegen. ";
                for (size_t j = 0; j < i; ++j)
                    if (!name.empty() && shares[j].first == name)
                        text += name + " steht doppelt in der Liste. ";
            }
            if (!text.empty())
                text.pop_back();
            return text;
        }

        // Share of a compound in output 1; 0 if it is not listed.
        double ShareOf(const std::string& name) const
        {
            for (const auto& entry : shares)
                if (entry.first == name)
                    return entry.second;
            return 0.0;
        }
    };

    // Compound separator: sends a fixed share of every compound to output 1,
    // independent of size, e.g. a magnet for iron scrap or hand sorting of
    // foreign matter. The size distribution of a compound is the same in
    // both outputs. A card with several compounds (only after coagulation)
    // goes with the mass-weighted mean of its compounds' shares. Continuum
    // and particle mode (account method per compound and size range):
    // CTwoWaySeparatorUnit.
    class CCompoundSeparatorUnit : public CTwoWaySeparatorUnit
    {
    public:
        const SCompoundSeparatorSettings& GetSettings() const { return m_settings; }

        // Takes the settings only if they are valid.
        bool SetSettings(const SCompoundSeparatorSettings& settings)
        {
            if (!settings.Check().empty())
                return false;
            m_settings = settings;
            return true;
        }

        std::string DescribeForLog() const override
        {
            std::string result;
            char text[64];
            for (const auto& entry : m_settings.shares)
                if (entry.second > 0.0)
                {
                    std::snprintf(text, sizeof(text), "%g", entry.second);
                    result += (result.empty() ? "" : ", ") + entry.first + " " + text;
                }
            for (char& c : result)
                if (c == '.')
                    c = ',';
            result = result.empty() ? "alle Stoffe in Ausgang 2 (Rest)" : "Anteil in Ausgang 1: " + result;
            if (IsParticleMode())
                result += "; Karten nach dem Konto-Verfahren";
            return result;
        }

        // Listed compounds that do not occur in the flowsheet (typing errors).
        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            for (const auto& entry : m_settings.shares)
                if (entry.second > 0.0 && std::find(m_compounds.begin(), m_compounds.end(), entry.first) == m_compounds.end())
                    warnings.push_back("Stoff " + entry.first + " kommt im Flowsheet nicht vor.");
            return warnings;
        }

    protected:
        void InitializeShares(const SSimulationGrid& grid) override
        {
            m_compounds = grid.compounds;
            m_compoundShare.assign(grid.GetNumCompounds(), 0.0);
            for (size_t c = 0; c < grid.compounds.size(); ++c)
                m_compoundShare[c] = m_settings.ShareOf(grid.compounds[c]);
        }

        double NodeShare(size_t compound, size_t /*sizeIdx*/) const override
        {
            return compound < m_compoundShare.size() ? m_compoundShare[compound] : 0.0;
        }

        double CardShare(const CStreamSlice& input, size_t card) const override
        {
            const int single = SingleCompound(input, card);
            if (single >= 0)
                return NodeShare(static_cast<size_t>(single), 0);
            double mass = 0.0, share = 0.0;
            for (size_t c = 0; c < input.GetNumCompounds(); ++c)
            {
                mass += input.GetCardMass(card, c);
                share += input.GetCardMass(card, c) * NodeShare(c, 0);
            }
            return mass > 0.0 ? std::clamp(share / mass, 0.0, 1.0) : 0.0;
        }

    private:
        SCompoundSeparatorSettings m_settings;
        std::vector<std::string> m_compounds; // active compounds of the last simulation
        std::vector<double> m_compoundShare;  // share in output 1 per compound index
    };
}
