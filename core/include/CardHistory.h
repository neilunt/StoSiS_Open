#pragma once
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>
#include "StreamSlice.h"

namespace stosis
{
    // Karten eines Stroms ueber die Zeit fuer die Kartenansicht (Methoden
    // Kap. 15): die Kartenzahl in jedem Zeitschritt und die Karten selbst
    // (Groesse, Massenstrom je Stoff). Gespeichert wird nur ein Lauf (Lauf 1).
    // Damit der Speicher begrenzt bleibt, gibt es eine Obergrenze: Wird sie
    // ueberschritten, bleibt nur noch jeder zweite gespeicherte Schritt
    // (Abstand verdoppelt), und es wird mit dem neuen Abstand weiter
    // gespeichert. Die Kartenzahl je Schritt bleibt immer vollstaendig.
    class CCardHistory
    {
    public:
        struct SSnapshot
        {
            size_t step = 0;
            std::vector<double> sizes;      // m
            std::vector<double> massFlows;  // kg/s, [Karte * Stoffe + Stoff]
        };

        // dt = Abstand der Zeitschritte in s (fuer die Zeitachse der Anzeige).
        void Reset(size_t numSteps, size_t numCompounds, size_t budgetBytes, double dt = 0.0)
        {
            m_dt = dt;
            m_counts.assign(numSteps, 0);
            m_numCompounds = std::max<size_t>(numCompounds, 1);
            m_budgetBytes = budgetBytes;
            m_stride = 1;
            m_bytes = 0;
            m_snapshots.clear();
        }

        void Record(size_t step, const CStreamSlice& slice)
        {
            if (step >= m_counts.size())
                return;
            const size_t numCards = slice.GetNumCards();
            m_counts[step] = numCards;
            if (step % m_stride != 0)
                return;
            SSnapshot snapshot;
            snapshot.step = step;
            snapshot.sizes.resize(numCards);
            snapshot.massFlows.assign(numCards * m_numCompounds, 0.0);
            const size_t numC = std::min(m_numCompounds, slice.GetNumCompounds());
            for (size_t k = 0; k < numCards; ++k)
            {
                snapshot.sizes[k] = slice.GetCardSize(k);
                for (size_t c = 0; c < numC; ++c)
                    snapshot.massFlows[k * m_numCompounds + c] = slice.GetCardMassFlow(k, c);
            }
            m_bytes += GetBytes(snapshot);
            m_snapshots.push_back(std::move(snapshot));
            while (m_bytes > m_budgetBytes && m_snapshots.size() > 1)
                Thin();
        }

        const std::vector<size_t>& GetCardCounts() const { return m_counts; }
        size_t GetNumSteps() const { return m_counts.size(); }
        double GetDt() const { return m_dt; }
        size_t GetNumCompounds() const { return m_numCompounds; }
        // Jeder wievielte Schritt gespeichert ist.
        size_t GetStride() const { return m_stride; }
        size_t GetStoredBytes() const { return m_bytes; }
        const std::vector<SSnapshot>& GetSnapshots() const { return m_snapshots; }

        // Gespeicherter Schritt, der step am naechsten liegt (nullptr = keiner).
        const SSnapshot* FindNearest(size_t step) const
        {
            if (m_snapshots.empty())
                return nullptr;
            auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), step,
                [](const SSnapshot& s, size_t value) { return s.step < value; });
            if (it == m_snapshots.end())
                return &m_snapshots.back();
            if (it != m_snapshots.begin() && step - std::prev(it)->step < it->step - step)
                --it;
            return &*it;
        }

    private:
        static size_t GetBytes(const SSnapshot& snapshot)
        {
            return sizeof(SSnapshot) + (snapshot.sizes.size() + snapshot.massFlows.size()) * sizeof(double);
        }

        void Thin()
        {
            m_stride *= 2;
            std::vector<SSnapshot> kept;
            kept.reserve(m_snapshots.size() / 2 + 1);
            m_bytes = 0;
            for (SSnapshot& snapshot : m_snapshots)
                if (snapshot.step % m_stride == 0)
                {
                    m_bytes += GetBytes(snapshot);
                    kept.push_back(std::move(snapshot));
                }
            m_snapshots.swap(kept);
        }

        std::vector<size_t> m_counts;
        std::vector<SSnapshot> m_snapshots;
        size_t m_numCompounds = 1;
        size_t m_budgetBytes = 0;
        size_t m_stride = 1;
        size_t m_bytes = 0;
        double m_dt = 0.0;
    };
}
