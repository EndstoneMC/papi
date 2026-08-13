"""Type stubs for the native PlaceholderAPI core."""

import enum

from endstone import OfflinePlayer, Player
from endstone.event import Event
from endstone.plugin import Plugin, Service, ServiceManager

__all__ = [
    "SERVICE_NAME",
    "ExpansionInfo",
    "ExpansionRegisteredEvent",
    "ExpansionUnregisteredEvent",
    "PlaceholderAPI",
    "PlaceholderExpansion",
    "UnregisterReason",
    "__version__",
]

__version__: str

SERVICE_NAME: str

class UnregisterReason(enum.Enum):
    """Why an expansion was removed from the registry."""

    EXPLICIT = 0
    OWNER_DISABLED = 1
    REQUIRED_PLUGIN_DISABLED = 2
    PAPI_SHUTDOWN = 3

class ExpansionInfo:
    """An immutable copy of a registered expansion's metadata.

    Stays valid and unchanged after the expansion it describes is unregistered and
    destroyed. It holds no reference to the expansion, its owner, or the registry.
    """

    @property
    def identifier(self) -> str:
        """The canonical, lowercase identifier."""

    @property
    def name(self) -> str:
        """The expansion's display name."""

    @property
    def author(self) -> str:
        """The expansion's author."""

    @property
    def version(self) -> str:
        """The expansion's version."""

    @property
    def owner(self) -> str:
        """The name of the plugin that registered the expansion."""

    @property
    def required_plugin(self) -> str | None:
        """The plugin the expansion requires, or None."""

    @property
    def relational(self) -> bool:
        """Whether the expansion handles relational placeholders."""

class PlaceholderExpansion:
    """Base class for placeholder providers.

    Subclass this and register it through the PlaceholderAPI service. C++ and Python
    expansions share one registry, so a consumer never learns which language a
    provider was written in.

    Metadata is read once at registration and copied, so an implementation must report
    stable values. Arguments are borrowed for the duration of a call only; do not keep
    the player past the callback.
    """

    def __init__(self) -> None: ...
    @property
    def identifier(self) -> str:
        """The identifier this expansion answers to.

        Must match ``[A-Za-z0-9][A-Za-z0-9.-]*``, and is canonicalized to lowercase.
        Underscore separates identifier from parameters and is not allowed here.
        """

    @property
    def author(self) -> str:
        """The expansion's author. Must not be empty."""

    @property
    def version(self) -> str:
        """The expansion's version. Must not be empty."""

    @property
    def name(self) -> str:
        """The expansion's display name. Defaults to the identifier."""

    @property
    def required_plugin(self) -> str | None:
        """The plugin this expansion requires, or None.

        Matched case-sensitively. It must be enabled at registration, and the expansion
        is removed automatically if it is later disabled.
        """

    def can_register(self) -> bool:
        """Final chance to refuse registration. Defaults to True."""

    def supports_relational_placeholders(self) -> bool:
        """Whether ``on_relational_request`` is implemented. Defaults to False."""

    def supports_player_cleanup(self) -> bool:
        """Whether ``on_player_quit`` is implemented. Defaults to False."""

    def on_request(self, player: OfflinePlayer | None, params: str) -> str | None:
        """Resolves an ordinary placeholder.

        Return a ``str``, or None to leave the placeholder text untouched. Any other
        type is a provider error and is not coerced.
        """

    def on_relational_request(self, one: Player, two: Player, params: str) -> str | None:
        """Resolves a relational placeholder between two online players."""

    def on_player_quit(self, player: Player) -> None:
        """Called when a player leaves, if this expansion opted into cleanup."""

    def on_unregister(self, reason: UnregisterReason) -> None:
        """Called once after this expansion has been removed from the registry."""

class PlaceholderAPI(Service):
    """Resolves placeholders through registered expansions.

    Obtained from Endstone's service manager; it cannot be constructed or subclassed in
    Python. A retained instance becomes permanently inert once PAPI is disabled:
    ``active`` turns False, parsing returns its input unchanged, queries come back
    empty, and mutations fail.

    Parsing and registration must happen on the server thread, because they call into
    provider code.
    """

    @staticmethod
    def load(service_manager: ServiceManager) -> PlaceholderAPI | None:
        """Load the active native service, or None when it is unavailable or shadowed."""

    @property
    def active(self) -> bool:
        """Whether this service is still usable."""

    def set_placeholders(self, player: OfflinePlayer | None, text: str) -> str:
        """Replaces every resolvable ``{identifier_params}`` in text."""

    def set_relational_placeholders(self, one: Player, two: Player, text: str) -> str:
        """Replaces every resolvable ``{rel_identifier_params}`` in text."""

    def contains_placeholders(self, text: str) -> bool:
        """Whether text lexically contains a placeholder-shaped substring."""

    def is_registered(self, identifier: str) -> bool:
        """Whether an identifier currently has an expansion registered."""

    @property
    def registered_identifiers(self) -> tuple[str, ...]:
        """Every registered identifier, sorted canonically."""

    @property
    def expansions(self) -> tuple[ExpansionInfo, ...]:
        """Metadata for every registered expansion, sorted by identifier."""

    def register_expansion(self, owner: Plugin, expansion: PlaceholderExpansion) -> bool:
        """Registers an expansion on behalf of a plugin."""

    def unregister_expansion(self, owner: Plugin, identifier: str) -> bool:
        """Unregisters one expansion owned by a plugin."""

    def unregister_expansions(self, owner: Plugin) -> int:
        """Unregisters every expansion owned by a plugin."""

class ExpansionRegisteredEvent(Event):
    """Fired after an expansion has been added to the registry."""

    @property
    def expansion_info(self) -> ExpansionInfo:
        """Metadata describing the expansion that was registered."""

class ExpansionUnregisteredEvent(Event):
    """Fired after an expansion has been removed from the registry."""

    @property
    def expansion_info(self) -> ExpansionInfo:
        """Metadata describing the expansion that was removed."""

    @property
    def reason(self) -> UnregisterReason:
        """Why the expansion was removed."""

class _PapiBootstrap:
    """Internal native lifecycle state owned by the PAPI plugin."""

    def start(self, plugin: Plugin) -> bool: ...
    def stop(self) -> None: ...
    @property
    def service(self) -> PlaceholderAPI | None: ...
