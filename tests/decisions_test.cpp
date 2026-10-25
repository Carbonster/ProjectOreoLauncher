// decisions_test.cpp - what the launcher decides from what it found: a component's state and
// button, what a mod card offers, the order of the cards, and whether the window opens.
#include <iostream>
#include <string>
#include <vector>

#include "app/component_status.h"
#include "app/mod_readiness.h"
#include "app/window_rule.h"
#include "model/component_ids.h"
#include "ui/card_order.h"

namespace {

using namespace oreo;

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
    return false;
}

Component component(const char* id, const std::string& installed, const std::string& latest) {
    Component c;
    c.id = id;
    c.installed = installed;
    c.latest = latest;
    return c;
}

ComponentFacts healthy_facts() {
    ComponentFacts facts;
    facts.python_available = true;
    facts.game_found = true;
    facts.game_build = "180836";
    return facts;
}

ModInfo python_mod(const std::string& name, bool needs_runtime = false, bool enabled = true) {
    ModInfo mod;
    mod.name = name;
    mod.folder = name;
    mod.path = "C:\\MODS\\" + name;
    mod.is_python = true;
    mod.enabled = enabled;
    if (needs_runtime) mod.tested_runtime = "0.3.1";
    return mod;
}

// NMSpy, pyMHF and the Runtime, each with the action given.
AppSnapshot snapshot_with(ActionKind nmspy, ActionKind pymhf, ActionKind runtime) {
    AppSnapshot snap;
    snap.checking = false;
    for (auto [id, action] : {std::pair<const char*, ActionKind>{kIdNmspy, nmspy}, {kIdPymhf, pymhf}, {kIdRuntime, runtime}}) {
        Component c = component(id, "1.0", "1.0");
        c.action = action;
        snap.components.push_back(c);
    }
    return snap;
}

// ---- component status --------------------------------------------------------------------

void a_newer_release_offers_update() {
    Component c = component(kIdRuntime, "0.1.5", "0.1.6");
    evaluate_component(c, healthy_facts());
    expect(c.state == CompState::UpdateAvailable && c.action == ActionKind::Update,
           "a newer release should mean UPDATE");
}

void a_missing_package_offers_install() {
    Component c = component(kIdNmspy, "", "181442.1");
    evaluate_component(c, healthy_facts());
    expect(c.state == CompState::NotInstalled && c.action == ActionKind::Install,
           "a package that is not there should mean INSTALL");
}

void nmspy_for_a_newer_game_build_offers_nothing() {
    Component c = component(kIdNmspy, "181442.1", "181442.1");
    evaluate_component(c, healthy_facts());
    expect(c.state == CompState::InstalledNewerThanTested, "NMSpy for a newer game build should say so");
    expect(c.action == ActionKind::None, "there is nothing the player can install for a game that is older");
}

void missing_python_is_named_as_the_cause() {
    ComponentFacts facts = healthy_facts();
    facts.python_available = false;
    facts.python_error = "python.exe is gone";
    Component c = component(kIdPymhf, "", "");
    evaluate_component(c, facts);
    expect(c.state == CompState::CheckFailed && c.error == "python.exe is gone",
           "a package should report the missing Python, not itself as broken");
}

void a_busy_component_keeps_its_state() {
    Component c = component(kIdRuntime, "0.1.5", "0.1.6");
    c.busy = true;
    c.state = CompState::Updating;
    evaluate_component(c, healthy_facts());
    expect(c.state == CompState::Updating, "the operation running on a component owns its state");
}

// ---- what a card offers --------------------------------------------------------------------

void install_wins_over_update_on_a_card() {
    AppSnapshot snap = snapshot_with(ActionKind::Update, ActionKind::None, ActionKind::Install);
    snap.mods.push_back(python_mod("Fishing", true));
    expect(card_button(snap, 0) == CardButton::Install, "anything missing should make the card say INSTALL");
}

void pymhf_is_never_updated_from_a_card() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::Update, ActionKind::None);
    snap.mods.push_back(python_mod("Fishing"));
    expect(card_button(snap, 0) == CardButton::None, "pip picks pyMHF; a card must not update it");
}

void the_runtime_matters_only_to_mods_that_ask() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::Install);
    snap.mods.push_back(python_mod("Logic only"));
    snap.mods.push_back(python_mod("Overlay", true));
    expect(card_button(snap, 0) == CardButton::None, "a mod without the Runtime should not ask for it");
    expect(card_button(snap, 1) == CardButton::Install, "a mod that needs the Runtime should offer INSTALL");
}

void a_mods_own_update_comes_last() {
    AppSnapshot snap = snapshot_with(ActionKind::Update, ActionKind::None, ActionKind::None);
    snap.mods.push_back(python_mod("Fishing"));
    snap.mod_updates[0] = AppSnapshot::UpdateState::Available;
    std::vector<PrepareStep> steps = prepare_steps(snap, 0);
    expect(steps.size() == 2 && steps[0].component_id == kIdNmspy && steps[1].component_id.empty(),
           "what a mod runs on comes before the mod itself");
}

// ---- whether the window opens ----------------------------------------------------------------

void the_window_opens_for_a_button_on_a_card() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::None);
    snap.mods.push_back(python_mod("Fishing", false, false));
    snap.mod_updates[0] = AppSnapshot::UpdateState::Available;
    expect(decide_window(snap, WindowRule()).show, "an update for a switched-off mod should open the window");
}

void the_window_stays_closed_for_what_the_player_cannot_act_on() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::None);
    snap.components[0].state = CompState::InstalledNewerThanTested;
    snap.mods.push_back(python_mod("Fishing"));
    expect(!decide_window(snap, WindowRule()).show, "a build mismatch alone should not open the window");
}

void always_show_opens_it_anyway() {
    AppSnapshot snap;
    WindowRule rule;
    rule.always_show = true;
    ShowReason reason = decide_window(snap, rule);
    expect(reason.show && reason.summary == "Everything is ready", "AlwaysShowWindow should open the window");
}

// ---- the order of the cards ----------------------------------------------------------------

std::vector<std::string> names(const AppSnapshot& snap, const std::vector<size_t>& order) {
    std::vector<std::string> out;
    for (size_t i : order) out.push_back(snap.mods[i].name);
    return out;
}

void cards_put_what_needs_attention_first() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::Install);
    snap.mods = {python_mod("Bravo", false, false), python_mod("Alpha"), python_mod("Delta", true),
                 python_mod("Charlie")};
    snap.mod_updates[3] = AppSnapshot::UpdateState::Available;
    CardOrder order;
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Delta", "Charlie", "Alpha", "Bravo"},
           "install, then update, then switched on, then switched off");
}

void cards_stay_put_until_refresh() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::None);
    snap.mods = {python_mod("Alpha"), python_mod("Bravo", false, false)};
    CardOrder order;
    order.order(snap);
    snap.mods[1].enabled = true;
    snap.mods[0].enabled = false;
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Alpha", "Bravo"},
           "flipping a switch must not move a card");
    order.resort();
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Bravo", "Alpha"},
           "REFRESH should sort again");
}

void a_new_mod_goes_last() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::None);
    snap.mods = {python_mod("Bravo")};
    CardOrder order;
    order.order(snap);
    snap.mods.push_back(python_mod("Alpha"));
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Bravo", "Alpha"},
           "a mod that turned up after sorting should go after the rest");
}

void no_sorting_while_something_runs() {
    AppSnapshot snap = snapshot_with(ActionKind::None, ActionKind::None, ActionKind::None);
    snap.mods = {python_mod("Bravo"), python_mod("Alpha", false, false)};
    snap.busy_with = "checking versions";
    CardOrder order;
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Alpha", "Bravo"},
           "while a check runs the cards are only in name order");
    snap.busy_with.clear();
    expect(names(snap, order.order(snap)) == std::vector<std::string>{"Bravo", "Alpha"},
           "the sort should happen once the check is over");
}

}  // namespace

int main() {
    a_newer_release_offers_update();
    a_missing_package_offers_install();
    nmspy_for_a_newer_game_build_offers_nothing();
    missing_python_is_named_as_the_cause();
    a_busy_component_keeps_its_state();
    install_wins_over_update_on_a_card();
    pymhf_is_never_updated_from_a_card();
    the_runtime_matters_only_to_mods_that_ask();
    a_mods_own_update_comes_last();
    the_window_opens_for_a_button_on_a_card();
    the_window_stays_closed_for_what_the_player_cannot_act_on();
    always_show_opens_it_anyway();
    cards_put_what_needs_attention_first();
    cards_stay_put_until_refresh();
    a_new_mod_goes_last();
    no_sorting_while_something_runs();

    if (g_failures) std::cerr << g_failures << " check(s) failed\n";
    return g_failures ? 1 : 0;
}
