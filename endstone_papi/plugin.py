"""Endstone plugin bootstrap for PlaceholderAPI.

This module owns no framework behavior. The service object, registry, parser, and
lifecycle all live in the native core; the plugin only creates the service, hands it
to Endstone, and shuts it down again.
"""

from __future__ import annotations

from endstone import Player
from endstone.command import Command, CommandSender
from endstone.plugin import Plugin

from ._papi import _PapiHost


class PlaceholderAPIPlugin(Plugin):
    api_version = "0.11"

    commands = {
        "papi": {
            "description": "PlaceholderAPI command",
            "usages": [
                "/papi parse <text: message>",
                "/papi parse <target: player> <text: message>",
                "/papi list",
                "/papi info <identifier: string>",
            ],
            "permissions": ["papi.command.papi"],
        }
    }

    permissions = {
        "papi.command.papi": {
            "description": "Allows the use of the /papi command",
            "default": "op",
        }
    }

    def __init__(self) -> None:
        super().__init__()
        self._host = _PapiHost()

    def on_enable(self) -> None:
        self._host.start(self)
        self.logger.info("PlaceholderAPI is ready.")

    def on_disable(self) -> None:
        # Native teardown must run here, while every provider module and the
        # interpreter are still loaded. Leaving it to garbage collection would let
        # expansions outlive the code that defines them.
        self._host.stop()

    def on_command(self, sender: CommandSender, command: Command, args: list[str]) -> bool:
        if not args:
            return False

        sub = args[0]
        rest = args[1:]

        if sub == "parse":
            return self._handle_parse(sender, rest)
        if sub == "list":
            return self._handle_list(sender)
        if sub == "info":
            return self._handle_info(sender, rest)

        sender.send_error_message(f"Unknown subcommand: {sub}")
        return True

    def _handle_parse(self, sender: CommandSender, args: list[str]) -> bool:
        if not args:
            sender.send_error_message("Usage: /papi parse [target] <text>")
            return True

        player = None
        text: str | None = None

        if len(args) >= 2:
            target = args[0]
            text = " ".join(args[1:])

            if target == "me":
                if not isinstance(sender, Player):
                    sender.send_error_message("Only a player can use 'me' as the target.")
                    return True
                player = sender
            elif target == "--null":
                player = None
            else:
                player = self.server.get_player(target)
                if player is None:
                    sender.send_error_message(f"Player not found: {target}")
                    return True
        else:
            text = args[0]
            if isinstance(sender, Player):
                player = sender

        assert text is not None
        service = self._host.service
        if service is None or not service.active:
            sender.send_error_message("PlaceholderAPI is not active.")
            return True

        sender.send_message(service.set_placeholders(player, text))
        return True

    def _handle_list(self, sender: CommandSender) -> bool:
        service = self._host.service
        if service is None or not service.active:
            sender.send_error_message("PlaceholderAPI is not active.")
            return True

        identifiers = service.registered_identifiers
        if not identifiers:
            sender.send_message("No expansions are registered.")
            return True

        sender.send_message(f"Registered expansions ({len(identifiers)}):")
        for identifier in identifiers:
            sender.send_message(f"  - {identifier}")
        return True

    def _handle_info(self, sender: CommandSender, args: list[str]) -> bool:
        if not args:
            sender.send_error_message("Usage: /papi info <identifier>")
            return True

        identifier = args[0]
        service = self._host.service
        if service is None or not service.active:
            sender.send_error_message("PlaceholderAPI is not active.")
            return True

        for info in service.expansions:
            if info.identifier == identifier.lower():
                sender.send_message(f"Identifier: {info.identifier}")
                sender.send_message(f"Name: {info.name}")
                sender.send_message(f"Author: {info.author}")
                sender.send_message(f"Version: {info.version}")
                sender.send_message(f"Owner: {info.owner}")
                sender.send_message(f"Required plugin: {info.required_plugin or '(none)'}")
                sender.send_message(f"Relational: {'yes' if info.relational else 'no'}")
                return True

        sender.send_error_message(f"No expansion registered as '{identifier}'.")
        return True
