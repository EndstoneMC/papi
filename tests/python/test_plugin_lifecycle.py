from __future__ import annotations

from typing import ClassVar

import endstone.metrics
import pytest

from endstone_papi import PlaceholderAPIPlugin


class StubBootstrap:
    def __init__(self, calls: list[str]) -> None:
        self.calls = calls

    def start(self, plugin: PlaceholderAPIPlugin) -> None:
        self.calls.append("start")

    def stop(self) -> None:
        self.calls.append("stop")


class StubLogger:
    def __init__(self, calls: list[str]) -> None:
        self.calls = calls

    def info(self, message: str) -> None:
        assert message == "PlaceholderAPI is ready."
        self.calls.append("ready")


class FakeMetrics:
    instances: ClassVar[list[FakeMetrics]] = []

    def __init__(self, plugin: PlaceholderAPIPlugin, *, service_id: int) -> None:
        self.plugin = plugin
        self.service_id = service_id
        plugin._bootstrap.calls.append("metrics")
        self.instances.append(self)


def make_plugin(calls: list[str]) -> PlaceholderAPIPlugin:
    plugin = PlaceholderAPIPlugin.__new__(PlaceholderAPIPlugin)
    plugin._bootstrap = StubBootstrap(calls)
    return plugin


def test_enable_starts_bootstrap_then_bstats(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[str] = []
    plugin = make_plugin(calls)

    FakeMetrics.instances.clear()
    monkeypatch.setattr(endstone.metrics, "Metrics", FakeMetrics)
    monkeypatch.setattr(
        PlaceholderAPIPlugin,
        "logger",
        property(lambda _: StubLogger(calls)),
    )

    plugin.on_enable()

    assert calls == ["start", "metrics", "ready"]
    assert len(FakeMetrics.instances) == 1
    assert plugin._metrics is FakeMetrics.instances[0]
    assert plugin._metrics.plugin is plugin
    assert plugin._metrics.service_id == 33349


def test_disable_stops_bootstrap() -> None:
    calls: list[str] = []
    plugin = make_plugin(calls)

    plugin.on_disable()

    assert calls == ["stop"]
