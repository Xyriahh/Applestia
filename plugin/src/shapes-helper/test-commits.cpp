#include "CommitSnapshots.hpp"
#include <hyprutils/memory/UniquePtr.hpp>
#include <cassert>
#include <cstdio>

using namespace Hyprutils::Memory;
struct SState { bool rejected = false; };
struct SAbaState {
    bool rejected = false;
    static void* operator new(size_t) {
        alignas(SAbaState) static unsigned char storage[sizeof(SAbaState)];
        return storage;
    }
    static void operator delete(void*) {}
};
using Snapshots = CShapeCommitSnapshots<SState>;
static applestia_shapes_state hints(float x) {
    applestia_shapes_state s{};
    s.count = 1;
    s.shapes[0].x = x;
    return s;
}
static void publish(Snapshots& snapshots, float& current) {
    if (auto* s = snapshots.staged()) {
        current = s->count ? s->shapes[0].x : -1;
        snapshots.published();
    }
}
int main() {
    Snapshots snapshots;
    SState pending;
    auto a = makeUnique<SState>(), b = makeUnique<SState>();
    snapshots.capturePending(hints(1)); snapshots.enqueue(a);
    snapshots.capturePending(hints(2)); snapshots.enqueue(b);
    float current = 0;
    snapshots.updated(*a, &pending); // A queued before B, applied after B precommit
    assert(snapshots.staged()->shapes[0].x == 1);
    publish(snapshots, current);
    assert(current == 1 && snapshots.queuedCount() == 1);
    snapshots.capturePending(hints(99)); // precommit rejected before enqueue
    assert(snapshots.queuedCount() == 1);
    snapshots.updated(*b, &pending); publish(snapshots, current);
    assert(current == 2); // rejected newer request cannot overwrite B
    snapshots.capturePending(hints(2)); snapshots.enqueue(b);
    b.reset(); // drop B without applying it: expiry, not FIFO popping
    snapshots.capturePending(hints(3));
    assert(snapshots.queuedCount() == 0);

    auto d = makeUnique<SState>(), e = makeUnique<SState>();
    snapshots.capturePending(hints(4)); snapshots.enqueue(d);
    snapshots.capturePending(hints(5)); snapshots.enqueue(e);
    snapshots.updated(*e, &pending); publish(snapshots, current);
    assert(current == 5 && snapshots.queuedCount() == 1);
    snapshots.updated(*d, &pending); publish(snapshots, current);
    assert(current == 4 && snapshots.queuedCount() == 0); // identity, not order

    auto f = makeUnique<SState>(), g = makeUnique<SState>();
    snapshots.capturePending(hints(6)); snapshots.enqueue(f);
    f->rejected = true; // rejection after stateCommit listener, before scheduleState
    snapshots.capturePending(hints(7)); snapshots.enqueue(g);
    assert(snapshots.queuedCount() == 1);
    snapshots.updated(*g, &pending); publish(snapshots, current);
    assert(current == 7);

    // A state rejected during stateCommit itself never gains an association.
    f->rejected = true;
    snapshots.capturePending(hints(6)); snapshots.enqueue(f);
    assert(snapshots.queuedCount() == 0);

    // Synchronized child: actual m_current update stages A, but parent commit
    // is what publishes it. B's queued hints/pending changes cannot replace A.
    a = makeUnique<SState>(); b = makeUnique<SState>();
    snapshots.capturePending(hints(1)); snapshots.enqueue(a);
    snapshots.capturePending(hints(2)); snapshots.enqueue(b);
    snapshots.updated(*a, &pending);
    assert(current == 7);
    snapshots.capturePending(hints(3)); // uncommitted child changes
    publish(snapshots, current); // synchronized parent's applied commit
    assert(current == 1);
    snapshots.updated(*b, &pending);
    assert(current == 1);
    publish(snapshots, current);
    assert(current == 2);
    publish(snapshots, current); // ancestor commit with no new child state
    assert(current == 2);

    // Null buffer uses m_pending directly and drops all queued states.
    snapshots.capturePending(hints(4)); snapshots.enqueue(d);
    snapshots.capturePending(hints(8));
    snapshots.updated(pending, &pending);
    assert(snapshots.queuedCount() == 0);
    publish(snapshots, current);
    assert(current == 8);
    auto old = makeUnique<SState>(); // state predates association / no snapshot
    snapshots.updated(*old, &pending); publish(snapshots, current);
    assert(current == -1); // empty rather than accidentally taking pending 8

    // Address ABA: expired identities at an address cannot select old metadata.
    CShapeCommitSnapshots<SAbaState> aba;
    auto owner1 = makeUnique<SAbaState>();
    auto* address = owner1.get();
    aba.capturePending(hints(9)); aba.enqueue(owner1);
    owner1.reset();
    auto owner2 = makeUnique<SAbaState>();
    assert(owner2.get() == address);
    aba.updated(*owner2, nullptr);
    assert(aba.staged()->count == 0);
    std::puts("shape commits: queued A/B, drop/rejection, out-of-order identity, synchronized publish, null commit and ABA passed");
}
