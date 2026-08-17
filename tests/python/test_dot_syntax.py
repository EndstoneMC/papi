from __future__ import annotations

import pytest

try:
    import endstone_papi._papi  # noqa: F401
except ImportError:
    pytest.skip("native module not built", allow_module_level=True)

from endstone_papi import PlaceholderExpansion
from endstone_papi._papi import _TestService


def test_python_provider_receives_underscore_key_from_dot_syntax() -> None:
    seen: list[str] = []

    class SparkExpansion(PlaceholderExpansion):
        identifier = "spark"
        author = "test"
        version = "1"

        def on_request(self, player, params):
            seen.append(params)
            return params

    host = _TestService("dot-syntax")
    assert host.register_expansion(SparkExpansion())

    assert host.service.set_placeholders(None, "{spark.cpu_process_1m}") == "cpu_process_1m"
    assert seen == ["cpu_process_1m"]


def test_old_underscore_separator_is_not_recognized() -> None:
    class SparkExpansion(PlaceholderExpansion):
        identifier = "spark"
        author = "test"
        version = "1"

        def on_request(self, player, params):
            return "unexpected"

    host = _TestService("strict-syntax")
    assert host.register_expansion(SparkExpansion())

    token = "{spark_cpu_process_1m}"
    assert host.service.set_placeholders(None, token) == token


def test_relational_dot_syntax_uses_rel_namespace() -> None:
    class FriendsExpansion(PlaceholderExpansion):
        identifier = "friends"
        author = "test"
        version = "1"

        def supports_relational_placeholders(self):
            return True

        def on_request(self, player, params):
            return None

        def on_relational_request(self, one, two, params):
            return params

    host = _TestService("dot-relational")
    assert host.register_expansion(FriendsExpansion())

    assert host.set_relational_placeholders("{rel.friends_since_1m}") == "since_1m"
