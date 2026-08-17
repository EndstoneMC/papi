from __future__ import annotations

import pytest

try:
    import endstone_papi._papi  # noqa: F401
except ImportError:
    pytest.skip("native module not built", allow_module_level=True)

from endstone_papi import PlaceholderExpansion
from endstone_papi._papi import _TestService


def test_python_provider_receives_params_from_colon_syntax_verbatim() -> None:
    seen: list[str] = []

    class SparkExpansion(PlaceholderExpansion):
        identifier = "spark"
        author = "test"
        version = "1"

        def on_request(self, player, params):
            seen.append(params)
            return params

    host = _TestService("colon-syntax")
    assert host.register_expansion(SparkExpansion())

    assert host.service.set_placeholders(None, "{spark:cpu_process.1m:raw}") == "cpu_process.1m:raw"
    assert seen == ["cpu_process.1m:raw"]


def test_legacy_outer_separators_are_not_recognized() -> None:
    class SparkExpansion(PlaceholderExpansion):
        identifier = "spark"
        author = "test"
        version = "1"

        def on_request(self, player, params):
            return "unexpected"

    host = _TestService("strict-colon-syntax")
    assert host.register_expansion(SparkExpansion())

    for token in ("{spark_cpu_process_1m}", "{spark.cpu_process_1m}"):
        assert host.service.set_placeholders(None, token) == token


def test_relational_colon_syntax_uses_rel_prefix_and_second_colon() -> None:
    seen: list[str] = []

    class FriendsExpansion(PlaceholderExpansion):
        identifier = "friends"
        author = "test"
        version = "1"

        def supports_relational_placeholders(self):
            return True

        def on_request(self, player, params):
            return None

        def on_relational_request(self, one, two, params):
            seen.append(params)
            return params

    host = _TestService("colon-relational")
    assert host.register_expansion(FriendsExpansion())

    assert host.set_relational_placeholders("{rel:friends:since_1m}") == "since_1m"
    assert host.set_relational_placeholders("{rel:friends:a_b.c:d}") == "a_b.c:d"
    assert seen == ["since_1m", "a_b.c:d"]

    for token in ("{rel.friends_since_1m}", "{rel:friends_since_1m}"):
        assert host.set_relational_placeholders(token) == token
    assert seen == ["since_1m", "a_b.c:d"]


def test_rel_identifier_is_reserved() -> None:
    class RelExpansion(PlaceholderExpansion):
        identifier = "ReL"
        author = "test"
        version = "1"

        def on_request(self, player, params):
            return "shadowed"

    host = _TestService("reserved-rel")
    assert host.register_expansion(RelExpansion()) is False
    token = "{rel:friends:is_friend}"
    assert host.service.set_placeholders(None, token) == token
