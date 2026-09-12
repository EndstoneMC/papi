from dataclasses import dataclass, field
from typing import ClassVar

from endstone.plugin import Plugin

from endstone_papi import PlaceholderAPI, PlaceholderExpansion, UnregisterReason


@dataclass
class ProbeState:
    requests: int = 0
    reasons: list[UnregisterReason] = field(default_factory=list)
    disables: int = 0


owner_state = ProbeState()


class ProbeExpansion(PlaceholderExpansion):
    author = "migration-test"
    version = "1.0.0"

    def __init__(self, identifier: str, state: ProbeState) -> None:
        super().__init__()
        self._identifier = identifier
        self.state = state

    @property
    def identifier(self) -> str:
        return self._identifier

    def on_request(self, player, params):
        if player is not None:
            raise AssertionError("migration probe requires a null player")
        self.state.requests += 1
        return f"probe:{params}"

    def on_unregister(self, reason: UnregisterReason) -> None:
        self.state.reasons.append(reason)


class MigrationOwner(Plugin):
    api_version = "0.12"
    soft_depend: ClassVar[list[str]] = ["papi"]

    def on_enable(self) -> None:
        service = PlaceholderAPI.load(self.server.service_manager)
        if service is None or not service.register_expansion(self, ProbeExpansion("migration-owner", owner_state)):
            raise RuntimeError("PAPI_MIGRATION_OWNER_FAIL registration")
        self.logger.info("PAPI_MIGRATION_OWNER_READY")

    def on_disable(self) -> None:
        owner_state.disables += 1
