from __future__ import annotations

from endstone.event import EventPriority, PlayerJoinEvent, event_handler
from endstone.plugin import Plugin

from endstone_papi import PlaceholderAPI, PlaceholderExpansion


class NameExpansion(PlaceholderExpansion):
    """A minimal provider expansion: answers {player_name}."""

    identifier = "player"
    author = "Endstone"
    version = "1.0.0"

    def on_request(self, player, params):
        if params != "name":
            return None
        return player.name if player is not None else "nobody"


class JoinExample(Plugin):
    api_version = "0.11"
    soft_depend = ["papi"]

    def __init__(self) -> None:
        super().__init__()
        self._api: PlaceholderAPI | None = None

    def on_enable(self) -> None:
        service = PlaceholderAPI.load(self.server.service_manager)
        if service is None or not service.active:
            self.logger.warning("PlaceholderAPI is unavailable; disabling example.")
            self.server.plugin_manager.disable_plugin(self)
            return

        self._api = service
        self._api.register_expansion(self, NameExpansion())
        self.register_events(self)

    def on_disable(self) -> None:
        if self._api is not None:
            self._api.unregister_expansions(self)

    @event_handler(priority=EventPriority.HIGHEST)
    def on_player_join(self, event: PlayerJoinEvent) -> None:
        if self._api is None:
            return
        message = self._api.set_placeholders(event.player, "{player_name} joined the server!")
        event.join_message = message
