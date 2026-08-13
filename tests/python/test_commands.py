"""Command metadata and event surface tests.

The command handler is exercised against stand-in senders rather than a live server,
because the point of these tests is that malformed input cannot assert or crash. Event
ordering and payload semantics are verified natively, where the registry can actually be
driven; here we only confirm the public event types expose copied metadata and nothing
callable.
"""

from __future__ import annotations

import endstone_papi
from endstone_papi import PlaceholderAPIPlugin


class RecordingSender:
    """Captures what the handler sent, standing in for a CommandSender."""

    def __init__(self) -> None:
        self.messages: list[str] = []
        self.errors: list[str] = []

    def send_message(self, message: str) -> None:
        self.messages.append(str(message))

    def send_error_message(self, message: str) -> None:
        self.errors.append(str(message))


class StubBootstrap:
    """Stands in for native lifecycle state so commands can be tested in isolation."""

    def __init__(self, service: object | None) -> None:
        self._service = service

    @property
    def service(self) -> object | None:
        return self._service


class StubService:
    def __init__(self, *, active: bool = True) -> None:
        self.active = active
        self.registered_identifiers = ["alpha", "beta"]
        self.expansions: list[object] = []
        self.parsed: list[tuple[object, str]] = []

    def set_placeholders(self, player: object, text: str) -> str:
        self.parsed.append((player, text))
        return f"parsed:{text}"


def make_plugin(service: object | None) -> PlaceholderAPIPlugin:
    """Builds a plugin whose native lifecycle state is replaced by a stub.

    The plugin is not constructed through Endstone, so ``__init__`` is bypassed; only the
    command handler is under test.
    """
    plugin = PlaceholderAPIPlugin.__new__(PlaceholderAPIPlugin)
    plugin._bootstrap = StubBootstrap(service)
    return plugin


def test_command_and_permission_metadata_is_declared() -> None:
    commands = PlaceholderAPIPlugin.commands
    assert "papi" in commands

    papi = commands["papi"]
    assert papi["permissions"] == ["papi.command.papi"]
    assert papi["usages"] == [
        "/papi (parse)<subcommand: PapiParseText> <text: message>",
        "/papi (parse)<subcommand: PapiParseTarget> <target: string> <text: message>",
        "/papi (list)<subcommand: PapiList>",
        "/papi (info)<subcommand: PapiInfo> <identifier: string>",
    ]

    # There is deliberately no reload command.
    assert all("reload" not in usage for usage in papi["usages"])

    permissions = PlaceholderAPIPlugin.permissions
    assert permissions["papi.command.papi"]["default"] == "op"


# Malformed input must never assert or raise.
def test_no_arguments_reports_usage_instead_of_raising() -> None:
    plugin = make_plugin(StubService())
    # Returning False makes Endstone print the declared usage.
    assert plugin.on_command(RecordingSender(), None, []) is False


def test_unknown_subcommand_is_reported() -> None:
    plugin = make_plugin(StubService())
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["nonsense"]) is True
    assert any("nonsense" in error for error in sender.errors)


def test_parse_without_text_reports_usage() -> None:
    plugin = make_plugin(StubService())
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["parse"]) is True
    assert any("Usage" in error for error in sender.errors)
    assert not sender.messages


def test_info_without_identifier_reports_usage() -> None:
    plugin = make_plugin(StubService())
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["info"]) is True
    assert any("Usage" in error for error in sender.errors)


def test_parse_target_preserves_a_multi_word_message_argument() -> None:
    service = StubService()
    plugin = make_plugin(service)
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["parse", "--null", "hello {a_b} world"]) is True
    assert service.parsed == [(None, "hello {a_b} world")]
    assert sender.messages == ["parsed:hello {a_b} world"]


def test_parse_without_target_preserves_a_multi_word_message_argument() -> None:
    service = StubService()
    plugin = make_plugin(service)
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["parse", "hello {a_b} world"]) is True
    assert service.parsed == [(None, "hello {a_b} world")]
    assert sender.messages == ["parsed:hello {a_b} world"]


def test_parse_with_explicit_null_target_passes_no_player() -> None:
    service = StubService()
    plugin = make_plugin(service)
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["parse", "--null", "{demo_x}"]) is True
    assert service.parsed == [(None, "{demo_x}")]


def test_parse_rejects_me_from_a_non_player_sender() -> None:
    service = StubService()
    plugin = make_plugin(service)
    sender = RecordingSender()

    # The console is not a player, so 'me' has no meaning and must be refused rather
    # than casting the sender.
    assert plugin.on_command(sender, None, ["parse", "me", "{demo_x}"]) is True
    assert any("player" in error.lower() for error in sender.errors)
    assert not service.parsed


def test_list_reports_every_identifier_deterministically() -> None:
    service = StubService()
    plugin = make_plugin(service)
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["list"]) is True
    joined = "\n".join(sender.messages)
    assert "alpha" in joined
    assert "beta" in joined
    # The count is reported so long lists remain comprehensible.
    assert any("2" in message for message in sender.messages)


def test_list_reports_an_empty_registry_clearly() -> None:
    service = StubService()
    service.registered_identifiers = []
    plugin = make_plugin(service)
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["list"]) is True
    assert any("No expansions" in message for message in sender.messages)


def test_info_reports_not_found_for_an_unknown_identifier() -> None:
    plugin = make_plugin(StubService())
    sender = RecordingSender()

    assert plugin.on_command(sender, None, ["info", "missing"]) is True
    assert any("missing" in error for error in sender.errors)


def test_commands_refuse_when_the_service_is_unavailable() -> None:
    for service in (None, StubService(active=False)):
        plugin = make_plugin(service)

        for args in (["parse", "--null", "x"], ["list"], ["info", "demo"]):
            sender = RecordingSender()
            assert plugin.on_command(sender, None, args) is True
            assert sender.errors, f"expected an error for {args}"
            assert not sender.messages


# An event exposes copied metadata and a reason, never a provider object.
def test_events_expose_only_metadata() -> None:
    for event_type in (endstone_papi.ExpansionRegisteredEvent, endstone_papi.ExpansionUnregisteredEvent):
        assert hasattr(event_type, "expansion_info")
        # No route from an event back to the live expansion.
        assert not hasattr(event_type, "expansion")

    assert hasattr(endstone_papi.ExpansionUnregisteredEvent, "reason")
    assert not hasattr(endstone_papi.ExpansionRegisteredEvent, "reason")


def test_expansion_info_fields_are_read_only() -> None:
    info_type = endstone_papi.ExpansionInfo

    for field in ("identifier", "name", "author", "version", "owner", "required_plugin", "relational"):
        descriptor = getattr(info_type, field, None)
        assert descriptor is not None, field
        # def_readonly bindings have no setter, so a consumer cannot mutate the copy.
        assert getattr(descriptor, "fset", None) is None, field
