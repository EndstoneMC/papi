import traceback
from typing import ClassVar

from endstone import Player
from endstone.command import Command, CommandSender
from endstone.event import event_handler
from endstone.plugin import Plugin
from endstone_papi_migration_owner import ProbeExpansion, ProbeState, owner_state

from endstone_papi import ExpansionRegisteredEvent, ExpansionUnregisteredEvent, PlaceholderAPI, UnregisterReason


class MigrationObserver(Plugin):
    api_version = "0.12"
    commands: ClassVar[dict] = {
        "migrationprobe": {
            "description": "Run the destructive test-only PAPI lifecycle probe",
            "usages": ["/migrationprobe (run)<action: MigrationProbeAction>"],
            "permissions": ["migrationprobe.run"],
        }
    }
    permissions: ClassVar[dict] = {"migrationprobe.run": {"description": "Run the migration probe", "default": "op"}}

    def __init__(self) -> None:
        super().__init__()
        self._ran = False
        self._service: PlaceholderAPI | None = None
        self._events: list[tuple[str, UnregisterReason]] = []
        self._registered = []
        self._removed = []

    def on_enable(self) -> None:
        self.register_events(self)
        self.logger.info("PAPI_MIGRATION_PROBE_READY command=migrationprobe run")

    @event_handler
    def on_expansion_registered(self, event: ExpansionRegisteredEvent) -> None:
        if event.expansion_info.identifier == "migration-shutdown":
            self._check(type(event) is ExpansionRegisteredEvent, "registered_concrete_type")
            self._registered.append(event.expansion_info)
            self.logger.info(f"PAPI_MIGRATION_EVENT {event.event_name} identifier=migration-shutdown")

    @event_handler
    def on_expansion_unregistered(self, event: ExpansionUnregisteredEvent) -> None:
        identifier = event.expansion_info.identifier
        if identifier in ("migration-owner", "migration-shutdown"):
            self._check(type(event) is ExpansionUnregisteredEvent, "unregistered_concrete_type")
            self._events.append((identifier, event.reason))
            self._removed.append(event.expansion_info)
            self.logger.info(f"PAPI_MIGRATION_EVENT {event.event_name} identifier={identifier} reason={event.reason}")

    def _check(self, condition: bool, label: str) -> None:
        if not condition:
            raise AssertionError(f"{label}; observed_events={self._events!r}")
        self.logger.info(f"PAPI_MIGRATION_CHECK_PASS {label}")

    def on_command(self, sender: CommandSender, command: Command, args: list[str]) -> bool:
        if args != ["run"]:
            return False
        try:
            self._check(not isinstance(sender, Player), "console_only")
            self._check(not self._ran, "fresh_probe")
            self._ran = True
            self._run()
        except Exception:
            self.logger.error(f"PAPI_MIGRATION_PROBE_FAIL\n{traceback.format_exc()}")
            raise
        self.logger.info("PAPI_MIGRATION_PROBE_PASS")
        return True

    def _run(self) -> None:
        manager = self.server.plugin_manager
        services = self.server.service_manager
        owner = manager.get_plugin("papi_migration_owner")
        papi = manager.get_plugin("papi")
        self._check(owner is not None and owner.is_enabled, "owner_enabled")
        self._check(papi is not None and papi.is_enabled, "papi_enabled")
        self._service = service = PlaceholderAPI.load(services)
        self._check(service is not None and service.active, "service_published")
        self._check(services.load("PlaceholderAPI") is not None, "named_service_published")
        token = "{migration-owner:Case_.:value}"
        self._check(service.set_placeholders(None, token) == "probe:Case_.:value", "python_resolution")
        self._check(owner_state.requests == 1 and owner_state.reasons == [], "owner_callback_once")

        manager.disable_plugin(owner)
        self._check(not owner.is_enabled and owner_state.disables == 1, "owner_disabled")
        self._check(owner_state.reasons == [UnregisterReason.OWNER_DISABLED], "owner_cleanup_once")
        self._check(not service.is_registered("migration-owner"), "owner_removed")
        self._check(service.set_placeholders(None, token) == token, "owner_literal_after_disable")
        self._check(owner_state.requests == 1, "owner_not_called_after_disable")
        self._check(self._events == [("migration-owner", UnregisterReason.OWNER_DISABLED)], "owner_event_once")

        state = ProbeState()
        self._check(self.is_enabled, "observer_survives_owner_disable")
        self._check(
            service.register_expansion(self, ProbeExpansion("migration-shutdown", state)),
            "shutdown_expansion_registered",
        )
        shutdown_token = "{migration-shutdown:alive}"
        self._check(len(self._registered) == 1, "registered_event_once")
        self._check(service.set_placeholders(None, shutdown_token) == "probe:alive", "shutdown_expansion_resolves")
        manager.disable_plugin(papi)
        self._check(not papi.is_enabled and self.is_enabled, "observer_survives_papi_disable")
        self._check(state.reasons == [UnregisterReason.PAPI_SHUTDOWN], "shutdown_callback_once")
        self._check(self._events == [("migration-owner", UnregisterReason.OWNER_DISABLED)], "shutdown_event_suppressed")
        self._check(PlaceholderAPI.load(services) is None, "typed_service_removed")
        self._check(services.load("PlaceholderAPI") is None, "named_service_removed")

        for _ in range(2):
            self._check(not service.active, "retained_inactive")
            self._check(service.set_placeholders(None, shutdown_token) == shutdown_token, "retained_parse_inert")
            self._check(service.expansions == () and service.registered_identifiers == (), "retained_metadata_empty")
            self._check(not service.is_registered("migration-shutdown"), "retained_query_empty")
            self._check(service.contains_placeholders(shutdown_token), "retained_lexical_query")
            rejected = ProbeState()
            self._check(
                not service.register_expansion(self, ProbeExpansion("migration-rejected", rejected)),
                "retained_register_rejected",
            )
            self._check(not service.unregister_expansion(self, "migration-shutdown"), "retained_unregister_rejected")
            self._check(service.unregister_expansions(self) == 0, "retained_bulk_unregister_empty")
            self._check(rejected.requests == 0 and rejected.reasons == [], "rejected_provider_untouched")
        self._check(state.requests == 1 and state.reasons == [UnregisterReason.PAPI_SHUTDOWN], "shutdown_not_repeated")
        self._check(owner_state.reasons == [UnregisterReason.OWNER_DISABLED], "owner_cleanup_not_repeated")
        self._check(
            [(info.identifier, info.owner, info.author, info.version) for info in self._registered]
            == [("migration-shutdown", self.name, "migration-test", "1.0.0")],
            "registered_metadata_retained",
        )
        self._check(
            [(info.identifier, info.owner, info.author, info.version) for info in self._removed]
            == [("migration-owner", "papi_migration_owner", "migration-test", "1.0.0")],
            "unregistered_metadata_retained",
        )
