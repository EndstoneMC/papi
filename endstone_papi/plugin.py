"""Endstone plugin bootstrap for PlaceholderAPI."""

from __future__ import annotations

from endstone.plugin import Plugin

from ._papi import _PapiHost


class PlaceholderAPIPlugin(Plugin):
    api_version = "0.11"

    def __init__(self) -> None:
        super().__init__()
        self._host = _PapiHost()

    def on_enable(self) -> None:
        self._host.start(self)
        self.logger.info("PlaceholderAPI is ready.")

    def on_disable(self) -> None:
        self._host.stop()
