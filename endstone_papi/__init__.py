"""Endstone PlaceholderAPI.

The registry, parser, and service implementation are native. This package provides
the Endstone plugin bootstrap plus the Python view of the native contract.

Consumers load the service from Endstone rather than constructing it:

    service = self.server.service_manager.load("PlaceholderAPI")
    if service is not None and service.active:
        service.register_expansion(self, MyExpansion())
        print(service.set_placeholders(player, "{demo_name}"))
"""

from ._papi import (
    SERVICE_NAME,
    ExpansionInfo,
    ExpansionRegisteredEvent,
    ExpansionUnregisteredEvent,
    PlaceholderAPI,
    PlaceholderExpansion,
    UnregisterReason,
    __version__,
)
from .plugin import PlaceholderAPIPlugin

__all__ = [
    "SERVICE_NAME",
    "ExpansionInfo",
    "ExpansionRegisteredEvent",
    "ExpansionUnregisteredEvent",
    "PlaceholderAPI",
    "PlaceholderAPIPlugin",
    "PlaceholderExpansion",
    "UnregisterReason",
    "__version__",
]
