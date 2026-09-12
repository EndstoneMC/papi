from types import SimpleNamespace

import pytest
from endstone.event import event_handler
from endstone.plugin import Plugin

from endstone_papi import ExpansionRegisteredEvent, ExpansionUnregisteredEvent, UnregisterReason
from endstone_papi._papi import _test_dispatch_event


@pytest.mark.parametrize("registered", [True, False])
def test_native_events_match_automatic_listeners_and_copy_metadata(registered):
    handlers = {}
    retained = []
    observed = []

    def register_event(name, executor, priority, plugin, ignore_cancelled):
        handlers[name] = executor

    class Listener:
        @event_handler
        def registered(self, event: ExpansionRegisteredEvent):
            observed.append((type(event), event.event_name, None))
            retained.append(event.expansion_info)
            return retained[-1]

        @event_handler
        def unregistered(self, event: ExpansionUnregisteredEvent):
            observed.append((type(event), event.event_name, event.reason))
            retained.append(event.expansion_info)
            return retained[-1]

    owner = SimpleNamespace(
        is_enabled=True,
        name="event-test",
        _listeners=[],
        server=SimpleNamespace(plugin_manager=SimpleNamespace(register_event=register_event)),
    )
    Plugin.register_events(owner, Listener())
    assert set(handlers) == {
        "endstone_papi.ExpansionRegisteredEvent",
        "endstone_papi.ExpansionUnregisteredEvent",
    }
    event_type = ExpansionRegisteredEvent if registered else ExpansionUnregisteredEvent
    name = f"endstone_papi.{event_type.__name__}"
    assert _test_dispatch_event(registered, handlers[name]), "metadata aliases the native stack event"
    assert observed == [(event_type, name, None if registered else UnregisterReason.OWNER_DISABLED)]
    info = retained[0]
    assert (
        info.identifier,
        info.name,
        info.author,
        info.version,
        info.owner,
        info.required_plugin,
        info.relational,
    ) == (
        "event-test",
        "Event Test",
        "tester",
        "1",
        "owner",
        "dependency",
        True,
    )
