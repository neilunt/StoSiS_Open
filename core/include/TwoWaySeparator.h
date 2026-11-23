#pragma once
#include <vector>
#include "SeparatorUnit.h"

namespace stosis
{
    // Separators with one input and two outputs that send a share T of the
    // feed to output 1 and the rest to output 2: compound separator (T by
    // compound) and classifier (T by settling velocity, i.e. size and
    // density). A derived unit only says what T is; continuum and particle
    // mode as in CSeparatorUnit, with the account share s of the card density
    // with flattening (ComputeCardShares). Cards with T = 0 or T = 1 go
    // straight to output 2 or 1.
    class CTwoWaySeparatorUnit : public CSeparatorUnit
    {
    public:
        CTwoWaySeparatorUnit() : CSeparatorUnit(2, true) {}

    protected:
        // Share of compound c at node s of the size grid that goes to output 1 (continuum).
        virtual double NodeShare(size_t compound, size_t sizeIdx) const = 0;
        // Share of a card that goes to output 1, between 0 and 1 (particle mode).
        virtual double CardShare(const CStreamSlice& input, size_t card) const = 0;

        void NodeShares(size_t compound, size_t sizeIdx, std::vector<double>& shares) const final
        {
            const double t = NodeShare(compound, sizeIdx);
            shares[0] = t;
            shares[1] = 1.0 - t;
        }

        void CardShares(const CStreamSlice& input, size_t card, std::vector<double>& shares) const final
        {
            const double t = CardShare(input, card);
            shares[0] = t;
            shares[1] = 1.0 - t;
        }
    };
}
