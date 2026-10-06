#pragma once

#include "applestia_shapes_helper_api.h"
#include <hyprutils/memory/WeakPtr.hpp>
#include <optional>
#include <unordered_map>

// No ownership of foreground states and no FIFO assumptions. Hyprland drops
// rejected states and clears its queue on null-buffer commits. Weak identities
// prevent both stale snapshots and allocator-address reuse from selecting hints.
template <typename State>
class CShapeCommitSnapshots {
  public:
    using Weak = Hyprutils::Memory::CWeakPointer<State>;

    void capturePending(const applestia_shapes_state& hints) {
        prune();
        m_pending = hints;
    }
    void enqueue(Weak state) {
        prune();
        if (!state.expired() && !state->rejected)
            m_queued.insert_or_assign(state.get(), SQueued{state, m_pending});
    }
    void updated(State& state, State* pending) {
        prune();
        if (&state == pending) {
            // wl_surface.attach(NULL) bypasses stateCommit/queue entirely and
            // clears all queued foreground states after updating m_current.
            m_staged = m_pending;
            m_queued.clear();
            return;
        }
        auto it = m_queued.find(&state);
        // An object created after this foreground commit was queued has no
        // snapshot for that commit. Empty, never newer pending hints, is correct.
        m_staged = it == m_queued.end() ? applestia_shapes_state{} : it->second.hints;
        if (it != m_queued.end())
            m_queued.erase(it);
    }
    const applestia_shapes_state* staged() const { return m_staged ? &*m_staged : nullptr; }
    void published() { m_staged.reset(); }
    size_t queuedCount() const { return m_queued.size(); }

  private:
    struct SQueued {
        Weak state;
        applestia_shapes_state hints;
    };
    void prune() {
        std::erase_if(m_queued, [](const auto& entry) {
            return entry.second.state.expired() || entry.second.state->rejected;
        });
    }
    applestia_shapes_state m_pending{};
    std::optional<applestia_shapes_state> m_staged;
    std::unordered_map<State*, SQueued> m_queued;
};
