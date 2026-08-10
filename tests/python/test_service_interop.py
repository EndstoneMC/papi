"""T-014: service-backed Python interop through the full native round-trip.

test_interop.py exercises the Python expansion contract directly (subclass, metadata,
return-type checks). test_parse_reentrancy.py exercises the error paths through the
service. What was missing is the happy path: a Python expansion registered through the
GIL-safe proxy, resolved by the native parser, and contained when it misbehaves - all
through the real PlaceholderAPI service, not by calling the expansion from Python.

Each test builds a fresh _TestService so the registry, throttle, and log buffer start
clean. The service is the same object a real consumer would receive from Endstone's
service manager.
"""

from __future__ import annotations

import pytest

try:
    import endstone_papi._papi  # noqa: F401
except ImportError:
    pytest.skip("native module not built", allow_module_level=True)

from endstone_papi import PlaceholderExpansion, UnregisterReason
from endstone_papi._papi import _TestService


@pytest.fixture()
def host() -> _TestService:
    return _TestService("test-plugin")


# --------------------------------------------------------------------------- #
# Happy path: register -> set_placeholders -> resolved value
# --------------------------------------------------------------------------- #


def test_register_and_resolve_round_trip(host: _TestService) -> None:
    """A Python expansion resolves through the native service."""

    class Greeter(PlaceholderExpansion):
        identifier = "greet"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            if params == "":
                return "hello"
            return f"hello:{params}"

    host.register_expansion(Greeter())
    service = host.service

    assert service.set_placeholders(None, "Hi {greet_}!") == "Hi hello!"
    assert service.set_placeholders(None, "Hi {greet_world}!") == "Hi hello:world!"


def test_params_preserve_case_and_content(host: _TestService) -> None:
    """Params arrive verbatim; the identifier is ASCII-lowercased."""

    captured: list[str] = []

    class Echo(PlaceholderExpansion):
        identifier = "Echo"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            captured.append(params)
            return params

    host.register_expansion(Echo())
    service = host.service

    # The identifier canonicalizes to "echo"; params keep their exact case.
    assert service.set_placeholders(None, "{echo_MixedCase}") == "MixedCase"
    assert captured == ["MixedCase"]


def test_multiple_expansions_resolve_independently(host: _TestService) -> None:
    """Two registered expansions each resolve in the same text."""

    class Alpha(PlaceholderExpansion):
        identifier = "alpha"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "A"

    class Beta(PlaceholderExpansion):
        identifier = "beta"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "B"

    host.register_expansion(Alpha())
    host.register_expansion(Beta())
    service = host.service

    assert service.set_placeholders(None, "{alpha_}|{beta_}") == "A|B"
    assert service.is_registered("alpha")
    assert service.is_registered("beta")
    assert service.registered_identifiers == ("alpha", "beta")


def test_mixed_resolved_and_unresolved_in_one_pass(host: _TestService) -> None:
    """Resolved tokens are replaced; unresolved ones are left intact."""

    class Known(PlaceholderExpansion):
        identifier = "known"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "ok"

    host.register_expansion(Known())
    service = host.service

    assert service.set_placeholders(None, "{known_}/{unknown_}") == "ok/{unknown_}"
    assert host.warnings == []


# --------------------------------------------------------------------------- #
# Containment: None, wrong type, exception - all through the service
# --------------------------------------------------------------------------- #


def test_none_return_preserves_token_silently(host: _TestService) -> None:
    """None means 'leave the placeholder alone' and is not an error."""

    class Decline(PlaceholderExpansion):
        identifier = "decline"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return None

    host.register_expansion(Decline())
    service = host.service

    assert service.set_placeholders(None, "[{decline_}]") == "[{decline_}]"
    assert host.warnings == []


def test_wrong_return_type_is_contained(host: _TestService) -> None:
    """A non-str, non-None return preserves the token and logs a provider error."""

    class BadType(PlaceholderExpansion):
        identifier = "badtype"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return 42

    host.register_expansion(BadType())
    service = host.service

    assert service.set_placeholders(None, "[{badtype_}]") == "[{badtype_}]"
    assert any("badtype" in w and "str or None" in w for w in host.warnings), host.warnings


def test_exception_is_contained(host: _TestService) -> None:
    """A raising override preserves the token and logs a provider error with context."""

    class Raiser(PlaceholderExpansion):
        identifier = "raiser"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            raise ValueError("deliberate failure")

    host.register_expansion(Raiser())
    service = host.service

    assert service.set_placeholders(None, "[{raiser_}]") == "[{raiser_}]"
    assert any("raiser" in w and "deliberate failure" in w for w in host.warnings), host.warnings


def test_unknown_identifier_preserves_token_silently(host: _TestService) -> None:
    """An unregistered identifier is ordinary text, not a provider error."""

    service = host.service
    assert service.set_placeholders(None, "[{ghost_}]") == "[{ghost_}]"
    assert host.warnings == []


# --------------------------------------------------------------------------- #
# Lexical and metadata queries
# --------------------------------------------------------------------------- #


def test_contains_placeholders_is_lexical(host: _TestService) -> None:
    """contains_placeholders is a cheap lexical check, independent of registration."""

    service = host.service
    assert service.contains_placeholders("Hello {world_}!")
    assert service.contains_placeholders("Hello {world}!")
    assert not service.contains_placeholders("Hello world!")
    assert not service.contains_placeholders("Hello {world")


def test_expansions_metadata_is_accessible(host: _TestService) -> None:
    """ExpansionInfo is exposed read-only through the service."""

    class WithMeta(PlaceholderExpansion):
        identifier = "meta"
        author = "author1"
        version = "2.0"
        required_plugin = "dep"

        def on_request(self, player, params):
            return None

    host.register_expansion(WithMeta())
    service = host.service

    infos = service.expansions
    assert len(infos) == 1
    info = infos[0]
    assert info.identifier == "meta"
    assert info.author == "author1"
    assert info.version == "2.0"
    assert info.owner == "test-plugin"
    assert info.required_plugin == "dep"
    assert info.relational is False


# --------------------------------------------------------------------------- #
# Unregister through the service
# --------------------------------------------------------------------------- #


def test_unregister_stops_resolution(host: _TestService) -> None:
    """After unregister, the token is no longer resolved."""

    class Temp(PlaceholderExpansion):
        identifier = "temp"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "temp"

    host.register_expansion(Temp())
    service = host.service

    assert service.set_placeholders(None, "{temp_}") == "temp"
    assert host.unregister_expansion("temp") is True
    assert service.is_registered("temp") is False
    assert service.set_placeholders(None, "{temp_}") == "{temp_}"


def test_unregister_invokes_callback_with_explicit_reason(host: _TestService) -> None:
    """on_unregister is called with EXPLICIT when unregister_expansion is used."""

    seen: list[UnregisterReason] = []

    class Tracked(PlaceholderExpansion):
        identifier = "tracked"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return None

        def on_unregister(self, reason):
            seen.append(reason)

    host.register_expansion(Tracked())
    host.unregister_expansion("tracked")

    assert seen == [UnregisterReason.EXPLICIT]


def test_unregister_all_clears_registry(host: _TestService) -> None:
    """unregister_expansions removes every owned expansion."""

    class A(PlaceholderExpansion):
        identifier = "a"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "a"

    class B(PlaceholderExpansion):
        identifier = "b"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "b"

    host.register_expansion(A())
    host.register_expansion(B())
    service = host.service

    assert service.registered_identifiers == ("a", "b")
    count = host.unregister_expansions()
    assert count == 2
    assert service.registered_identifiers == ()
    assert service.set_placeholders(None, "{a_}{b_}") == "{a_}{b_}"


def test_str_subclass_return_is_contained(host: _TestService) -> None:
    """A str subclass return violates the exact-str contract and is contained."""

    class S(str):
        pass

    class StrSubclassReturn(PlaceholderExpansion):
        identifier = "strsub"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return S("value")

    host.register_expansion(StrSubclassReturn())
    service = host.service

    assert service.set_placeholders(None, "[{strsub_}]") == "[{strsub_}]"
    assert any("strsub" in w and "str or None" in w for w in host.warnings), host.warnings


def test_snapshot_collections_are_frozen_tuples(host: _TestService) -> None:
    """registered_identifiers and expansions return immutable tuple snapshots."""

    class A(PlaceholderExpansion):
        identifier = "snap"
        author = "t"
        version = "1"

        def on_request(self, player, params):
            return "v"

    host.register_expansion(A())
    service = host.service

    ids = service.registered_identifiers
    infos = service.expansions

    assert type(ids) is tuple
    assert type(infos) is tuple
    assert ids == ("snap",)
    assert len(infos) == 1
    assert infos[0].identifier == "snap"

    # Tuples are immutable -- mutation must raise, proving the snapshot is frozen.
    try:
        ids[0] = "other"  # type: ignore[index]
        raise AssertionError("tuple mutation should have raised TypeError")
    except TypeError:
        pass
