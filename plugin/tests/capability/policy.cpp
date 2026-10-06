#include "../../src/ShapesCapability.hpp"
#include <cassert>
#include <iostream>
extern "C" {
void fixture_start();
int fixture_init();
void fixture_exit();
int fixture_active();
unsigned fixture_global_name();
void fixture_bind_old();
void fixture_assert_old_inert();
void fixture_bind_new();
void fixture_finish();
}

int main() {
    using namespace ShapesCapability;
    const std::unordered_set<std::string> all, drawers{"applestia-drawers"}, unrelated{"bar"};
    assert(eligible(true, true, all, all));
    assert(eligible(true, true, drawers, all));
    assert(!eligible(false, true, all, all));
    assert(!eligible(true, false, all, all));
    assert(!eligible(true, true, unrelated, all));
    assert(!eligible(true, true, drawers, drawers));
    fixture_start();
    SPolicy policy;
    auto sync = [&](bool enabled, const auto& include, const auto& exclude) {
        const bool result = policy.sync(eligible(enabled, true, include, exclude), fixture_active(),
                                        [] { return fixture_init(); }, [] { fixture_exit(); });
        assert(result == bool(fixture_active()));
        return result;
    };
    assert(!sync(true, all, all)); // No advertisement before plugin initialization.
    policy.started = true;
    assert(sync(true, drawers, all));
    fixture_bind_old();
    auto name = fixture_global_name();
    assert(!sync(false, drawers, all));
    fixture_assert_old_inert();
    assert(sync(true, drawers, all)); // Must create a real global, not just clear a flag.
    assert(fixture_global_name() != name);
    fixture_assert_old_inert();
    fixture_bind_new();
    name = fixture_global_name();
    assert(!sync(true, drawers, drawers)); // Exclude takes priority.
    assert(sync(true, drawers, all)); // Include again advertises another generation.
    assert(fixture_global_name() != name);
    name = fixture_global_name();
    assert(!sync(true, unrelated, all));
    assert(sync(true, drawers, all)); // Empty/explicit whitelist eligibility.
    assert(fixture_global_name() != name);
    policy.fatal = true;
    assert(!sync(true, drawers, all));
    assert(!sync(false, drawers, all));
    assert(!sync(true, drawers, all)); // Config toggles cannot recover terminal failures.
    SPolicy failing{.started = true};
    int attempts = 0;
    auto failedInit = [&] { ++attempts; return false; };
    assert(!failing.sync(true, false, failedInit, [] {}));
    assert(!failing.sync(false, false, failedInit, [] {}));
    assert(!failing.sync(true, false, failedInit, [] {}));
    assert(attempts == 1 && failing.fatal);
    fixture_finish();
    std::cout << "PASS real helper global off/on/exclude/include generations, old-object inertness, fatal latch/init-failure bounded retry\n";
}
