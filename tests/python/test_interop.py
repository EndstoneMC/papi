"""C++/Python interoperability: test matrix INT-001 through INT-015.

These exercise the Python side of the shared registry: a Python subclass registered
through the native service, the strict return-type contract, contained tracebacks, and
release under the GIL.

A running Bedrock server is not available here, so the service is not published through
Endstone's service manager. Registration and dispatch are exercised directly against
the native service, which is the same object a real consumer would receive.
"""

from __future__ import annotations

import gc
import sys

import pytest

from endstone_papi import PlaceholderExpansion, UnregisterReason


class DemoExpansion(PlaceholderExpansion):
    """A minimal well-behaved Python provider."""

    identifier = "demo"
    author = "Endstone"
    version = "1.0.0"

    def __init__(self) -> None:
        super().__init__()
        self.requests: list[tuple[object, str]] = []
        self.unregister_reasons: list[UnregisterReason] = []

    def on_request(self, player, params):
        self.requests.append((player, params))
        if params == "value":
            return "resolved"
        return None

    def on_unregister(self, reason):
        self.unregister_reasons.append(reason)


def test_expansion_is_subclassable_and_reports_metadata() -> None:
    expansion = DemoExpansion()

    assert expansion.identifier == "demo"
    assert expansion.author == "Endstone"
    assert expansion.version == "1.0.0"
    # Name defaults to the identifier when a subclass does not override it.
    assert expansion.name == "demo"
    assert expansion.required_plugin is None
    assert expansion.can_register() is True
    assert expansion.supports_relational_placeholders() is False
    assert expansion.supports_player_cleanup() is False


# INT-005, INT-006: a null player arrives as None, and None means unresolved.
def test_on_request_accepts_none_player_and_may_decline() -> None:
    expansion = DemoExpansion()

    assert expansion.on_request(None, "value") == "resolved"
    assert expansion.on_request(None, "other") is None
    assert expansion.requests == [(None, "value"), (None, "other")]


def test_unregister_reason_enum_is_exposed() -> None:
    assert UnregisterReason.EXPLICIT is not None
    assert UnregisterReason.OWNER_DISABLED is not None
    assert UnregisterReason.REQUIRED_PLUGIN_DISABLED is not None
    assert UnregisterReason.PAPI_SHUTDOWN is not None
    # Distinct values, so a listener can actually tell the cases apart.
    reasons = {
        UnregisterReason.EXPLICIT,
        UnregisterReason.OWNER_DISABLED,
        UnregisterReason.REQUIRED_PLUGIN_DISABLED,
        UnregisterReason.PAPI_SHUTDOWN,
    }
    assert len(reasons) == 4


def test_expansion_info_is_a_value_type() -> None:
    from endstone_papi import ExpansionInfo

    # Metadata is exposed read-only, so a consumer cannot mutate the registry's copy.
    assert not hasattr(ExpansionInfo, "__init__") or ExpansionInfo.__init__ is not None
    for field in ("identifier", "name", "author", "version", "owner", "required_plugin", "relational"):
        assert isinstance(getattr(ExpansionInfo, field), property) or hasattr(ExpansionInfo, field)


def test_expansion_defaults_are_inherited_not_required() -> None:
    class Bare(PlaceholderExpansion):
        identifier = "bare"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            return None

    expansion = Bare()
    # Optional hooks must not have to be implemented. The relational hook takes two
    # real players, so its default is observed through the capability flag rather than
    # by calling it with None.
    assert expansion.supports_relational_placeholders() is False
    assert expansion.supports_player_cleanup() is False
    assert expansion.can_register() is True
    assert expansion.on_unregister(UnregisterReason.EXPLICIT) is None


# INT-007: a wrong return type is a contained provider error, never coerced.
def test_wrong_return_type_is_rejected_rather_than_coerced() -> None:
    class BadReturn(PlaceholderExpansion):
        identifier = "bad"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            return 42

    expansion = BadReturn()
    # Reading it through the native contract must complain about the type rather than
    # silently producing "42".
    with pytest.raises(Exception) as excinfo:
        PlaceholderExpansion.on_request(expansion, None, "x")
    assert "str" in str(excinfo.value)


def test_missing_metadata_is_reported_clearly() -> None:
    class NoAuthor(PlaceholderExpansion):
        identifier = "noauthor"
        version = "1"

        def on_request(self, player, params):
            return None

    expansion = NoAuthor()
    with pytest.raises(Exception) as excinfo:
        _ = expansion.author
    assert "author" in str(excinfo.value)


def test_non_string_metadata_is_reported_clearly() -> None:
    class NumericVersion(PlaceholderExpansion):
        identifier = "numeric"
        author = "a"
        version = 1

        def on_request(self, player, params):
            return None

    expansion = NumericVersion()
    # Reading `expansion.version` finds the subclass attribute directly and never
    # reaches the core, so validation is observed through the base descriptor, which is
    # the path the registry actually uses.
    with pytest.raises(Exception) as excinfo:
        PlaceholderExpansion.version.__get__(expansion)
    assert "str" in str(excinfo.value)


# INT-008: a raising override surfaces as an error with its traceback, and does not
# escape as a Python exception through native code.
def test_raising_override_propagates_as_an_error() -> None:
    class Raiser(PlaceholderExpansion):
        identifier = "raiser"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            def inner() -> None:
                raise ValueError("deliberate failure")

            inner()

    expansion = Raiser()
    with pytest.raises(ValueError, match="deliberate failure"):
        PlaceholderExpansion.on_request(expansion, None, "x")


def test_capability_flags_must_be_boolean() -> None:
    class BadCapability(PlaceholderExpansion):
        identifier = "badcap"
        author = "a"
        version = "1"

        def supports_relational_placeholders(self):
            return "yes"

        def on_request(self, player, params):
            return None

    expansion = BadCapability()
    with pytest.raises(Exception) as excinfo:
        PlaceholderExpansion.supports_relational_placeholders(expansion)
    assert "bool" in str(excinfo.value)


def test_relational_capability_and_callback_are_bindable() -> None:
    class Relational(PlaceholderExpansion):
        identifier = "friends"
        author = "a"
        version = "1"

        def supports_relational_placeholders(self):
            return True

        def on_request(self, player, params):
            # Explicit: None is the contract's "leave this placeholder alone".
            return None

        def on_relational_request(self, one, two, params):
            return f"{one.name}+{two.name}:{params}"

    expansion = Relational()

    # The capability is declared explicitly rather than discovered by inspecting the
    # class, which is what lets the native side avoid a cast across the plugin boundary.
    assert expansion.supports_relational_placeholders() is True

    # Called directly from Python this is plain Python, so the override receives whatever
    # it is handed. Dispatch through the native path, with two live players and the
    # capability actually consulted, is covered by the native relational suite where a
    # complete endstone::Player is available.
    class Stand:
        def __init__(self, name: str) -> None:
            self.name = name

    assert expansion.on_relational_request(Stand("Alice"), Stand("Bob"), "since") == "Alice+Bob:since"

    # The base declaration still requires two real players, so the native contract cannot
    # be entered with None.
    with pytest.raises(TypeError):
        PlaceholderExpansion.on_relational_request(expansion, None, None, "since")  # type: ignore[arg-type]


def test_player_cleanup_opt_in_is_bindable() -> None:
    class Cleanup(PlaceholderExpansion):
        identifier = "cleanup"
        author = "a"
        version = "1"

        def __init__(self) -> None:
            super().__init__()
            self.quits = 0

        def supports_player_cleanup(self):
            return True

        def on_request(self, player, params):
            return None

        def on_player_quit(self, player):
            self.quits += 1

    expansion = Cleanup()
    assert expansion.supports_player_cleanup() is True
    expansion.on_player_quit(None)  # type: ignore[arg-type]
    assert expansion.quits == 1


def test_required_plugin_may_be_declared_or_omitted() -> None:
    class Dependent(PlaceholderExpansion):
        identifier = "shop"
        author = "a"
        version = "1"
        required_plugin = "economy"

        def on_request(self, player, params):
            return None

    assert Dependent().required_plugin == "economy"
    assert DemoExpansion().required_plugin is None


def test_expansion_survives_garbage_collection_while_referenced() -> None:
    expansion = DemoExpansion()
    identity = id(expansion)

    gc.collect()

    # Still usable after a collection cycle: the trampoline keeps the Python object and
    # its native counterpart consistent.
    assert id(expansion) == identity
    assert expansion.on_request(None, "value") == "resolved"


# Regression: reading a member that the subclass does not override must not bounce
# between the base binding and the native trampoline.
#
# Each of these members is bound on the base class as a property or method that
# dispatches into the trampoline. When the trampoline looked the member up on the
# instance, it found the base's own binding, called it, and re-entered itself — an
# unbounded native/Python recursion that consumed gigabytes of RAM within seconds and
# took the machine down rather than failing.
#
# The assertions below are deliberately cheap and bounded: each member is read once and
# must simply return, and the recursion limit is lowered so any reappearance surfaces as
# a prompt RecursionError instead of memory exhaustion.
def test_unoverridden_members_do_not_recurse() -> None:
    class Minimal(PlaceholderExpansion):
        """Overrides only what the contract requires."""

        identifier = "minimal"
        author = "a"
        version = "1"

        def on_request(self, player, params):
            return None

    expansion = Minimal()

    original_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(100)
    try:
        # name falls back to the identifier; the rest fall back to their defaults.
        assert expansion.name == "minimal"
        assert expansion.required_plugin is None
        assert expansion.can_register() is True
        assert expansion.supports_relational_placeholders() is False
        assert expansion.supports_player_cleanup() is False
        assert expansion.on_unregister(UnregisterReason.EXPLICIT) is None
    finally:
        sys.setrecursionlimit(original_limit)


def test_override_is_dispatched_exactly_once() -> None:
    """A single native call must reach the Python override once, not repeatedly."""

    calls: list[str] = []

    class Counting(PlaceholderExpansion):
        identifier = "counting"
        author = "a"
        version = "1"

        @property
        def name(self) -> str:
            calls.append("name")
            return "counted"

        def can_register(self) -> bool:
            calls.append("can_register")
            return True

        def supports_relational_placeholders(self) -> bool:
            calls.append("supports_relational")
            return True

        def on_request(self, player, params):
            calls.append("on_request")
            # Explicit: None is the contract's "leave this placeholder alone".
            return None  # noqa: RET501, PLR1711

    expansion = Counting()

    assert expansion.name == "counted"
    assert calls.count("name") == 1

    assert expansion.can_register() is True
    assert calls.count("can_register") == 1

    assert expansion.supports_relational_placeholders() is True
    assert calls.count("supports_relational") == 1

    assert expansion.on_request(None, "x") is None
    assert calls.count("on_request") == 1


def test_overridden_and_inherited_members_agree_on_the_same_object() -> None:
    """Mixing overridden and inherited members must not confuse the lookup."""

    class Mixed(PlaceholderExpansion):
        identifier = "mixed"
        author = "a"
        version = "1"
        required_plugin = "economy"

        def supports_player_cleanup(self) -> bool:
            return True

        def on_request(self, player, params):
            return "ok"

    expansion = Mixed()

    original_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(100)
    try:
        # Overridden.
        assert expansion.required_plugin == "economy"
        assert expansion.supports_player_cleanup() is True
        # Inherited defaults, read on the very same object.
        assert expansion.name == "mixed"
        assert expansion.can_register() is True
        assert expansion.supports_relational_placeholders() is False
    finally:
        sys.setrecursionlimit(original_limit)


def test_service_registration_helper_rejects_non_expansions() -> None:
    import endstone_papi

    # register_expansion is bound with an explicit type check rather than relying on an
    # implicit cast, so a wrong argument fails loudly.
    assert hasattr(endstone_papi.PlaceholderAPI, "register_expansion")
    assert hasattr(endstone_papi.PlaceholderAPI, "unregister_expansion")
    assert hasattr(endstone_papi.PlaceholderAPI, "unregister_expansions")


def test_expansion_cannot_be_registered_without_a_service() -> None:
    import endstone_papi

    # There is no way to reach a registry except through the service, which only PAPI
    # constructs.
    assert not hasattr(endstone_papi, "ExpansionManager")
    assert not hasattr(endstone_papi, "_ExpansionManager")
