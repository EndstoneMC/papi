# Placeholder API for Endstone

A PlaceholderAPI framework for [Endstone](https://github.com/EndstoneMC/endstone),
inspired by the PlaceholderAPI plugin for Spigot. C++ and Python plugins are equal
providers: both implement the same expansion contract and share one registry, so a
consumer never needs to know which language a placeholder came from.

## Install

This development tree targets Endstone API 0.12 at commit
`e7eab9222abb92103714837ebe26672ee213f688`. Its Python distribution version is
`0.11.11.dev392`; the package version and plugin API version are different.
Use the verified official wheels from Endstone build `33562961160`, matching your
Python version and platform. This snapshot is not assumed to be available on PyPI.

From a checkout with GitHub CLI authentication configured:

```shell
python tools/prepare_endstone_wheels.py --output-dir .endstone-wheelhouse
python -m pip install --find-links .endstone-wheelhouse --only-binary endstone "endstone==0.11.11.dev392"
python -m pip install --find-links .endstone-wheelhouse path/to/endstone_papi.whl
python -m pip check
```

Use the actual PAPI wheel filename in the second install command. For a server,
install the same Endstone snapshot in its environment, put the compatible PAPI
wheel in `plugins`, and restart. Production release publication is blocked until
the runtime is repinned to a supported stable Endstone distribution.

## Placeholder syntax

A placeholder is written `{identifier:params}`.

The **first colon** separates the identifier from the parameters. The identifier is
ASCII-lowercased and matched case-insensitively; the parameters are passed to the
expansion exactly as written, including underscores, dots, later colons, case, spaces,
and an empty value.

| Input                      | Identifier | Params             | Notes                                   |
| -------------------------- | ---------- | ------------------ | --------------------------------------- |
| `{player:name}`          | `player` | `name`           | the ordinary form                       |
| `{PLAYER:NaMe}`          | `player` | `NaMe`           | identifier lowercased, params preserved |
| `{spark:cpu_process_1m}` | `spark`  | `cpu_process_1m` | underscores belong to params            |
| `{player:}`              | `player` | *(empty)*        | dispatched with empty params            |
| `{player}`               | —         | —                 | no colon, so it stays literal           |

Anything that cannot be resolved is left exactly as written: malformed syntax, an unknown
identifier, an expansion that returns no value, or an expansion exception. An empty
string is a valid replacement. Replacement text is never re-scanned, so parsing is
one-pass and nonrecursive.

`{rel:identifier:params}` is a separate, relational form handled only by
`setRelationalPlaceholders` / `set_relational_placeholders`, and only by expansions that
opt in to the relational callback. The `rel:` prefix selects relational parsing. The next colon separates the relational
expansion identifier from its parameters; everything after that second colon is passed
through unchanged. For example, `{rel:friends:is_friend}` dispatches identifier `friends`
with params `is_friend`.

Identifiers must match `[A-Za-z0-9][A-Za-z0-9-]*`. Registration is case-insensitive,
so identifiers that differ only by case collide. Dot, underscore, and colon are invalid
in an identifier. `rel` is reserved by the relational syntax and cannot be registered
as an expansion identifier.

## The core provides no placeholders

PlaceholderAPI is a framework. It ships **no** built-in placeholders — no player name,
coordinates, ping, time, or economy values. Every placeholder comes from an expansion
registered by a plugin, which is what keeps ownership, permissions, and lifecycle with
the plugin that understands the data.

## Usage

### C++

> [!NOTE]
> The `endstone_papi` headers are packaged inside the `.whl`.

```c++
#include <endstone/endstone.hpp>
#include <endstone_papi/papi.h>

class NameExpansion final : public papi::PlaceholderExpansion {
public:
    [[nodiscard]] std::string getIdentifier() const override { return "player"; }
    [[nodiscard]] std::string getAuthor() const override { return "Endstone"; }
    [[nodiscard]] std::string getVersion() const override { return "1.0.0"; }

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::Player *player,
                                                       std::string_view params) override
    {
        if (params != "name") {
            return std::nullopt;  // unresolved: the placeholder is left as written
        }
        return player ? player->getName() : std::string("nobody");
    }
};

// In onEnable:
auto api = getServer().getServiceManager().load<papi::PlaceholderAPI>(
    std::string(papi::PlaceholderAPI::ServiceName));
if (api && api->isActive()) {
    api->registerExpansion(*this, std::make_shared<NameExpansion>());
}
```

Endstone 0.12 returns a `Nullable<PlaceholderAPI>` from the service loader. Check it
before use; `api.get()` returns a `shared_ptr` that can be copied to retain the
service. Retained references become inert when PAPI shuts down.

**Breaking migration:** ordinary callbacks now take `const endstone::Player*`
(`Player | None` in Python), replacing `OfflinePlayer`. Rebuild all native PAPI
consumers and providers against this SDK and the pinned Endstone snapshot with a
compatible toolchain; existing binaries are not ABI-compatible.

For the full code, see the [C++ example plugin](examples/cpp).

### Python

```python
from endstone.plugin import Plugin
from endstone_papi import PlaceholderAPI, PlaceholderExpansion


class NameExpansion(PlaceholderExpansion):
    identifier = "player"
    author = "Endstone"
    version = "1.0.0"

    def on_request(self, player, params):
        if params != "name":
            return None  # unresolved: the placeholder is left as written
        return player.name if player is not None else "nobody"


class MyPlugin(Plugin):
    api_version = "0.12"
    soft_depend = ["papi"]

    def on_enable(self):
        # The typed loader also rejects an unrelated service shadowing PAPI's name.
        service = PlaceholderAPI.load(self.server.service_manager)
        if service is None or not service.active:
            return
        service.register_expansion(self, NameExpansion())
```

For the full code, see the [Python example plugin](examples/python).

### Lifecycle

PAPI owns a registered expansion until it is unregistered, so a provider does not have
to keep its own reference. Only the owner can explicitly unregister its expansions.
PAPI also removes expansions when their owner or required plugin is disabled; calling
`unregister_expansions(self)` from `on_disable` is allowed and simply makes that explicit.

A retained service reference stays safe after PAPI is disabled: it becomes permanently
inert, so `active` turns False, parsing returns its input unchanged, queries come back
empty, and mutations fail. After a reload, the old reference remains inert; consumers
must load a fresh service.

### Threading and introspection

PAPI events use the dispatch names `endstone_papi.ExpansionRegisteredEvent` and
`endstone_papi.ExpansionUnregisteredEvent`. Python listeners use Endstone's normal
`@event_handler` and `register_events`; rebuilt C++ listeners use `EventType::NAME`.
Rebuild native listeners and update raw unqualified event names when migrating.
Event objects are borrowed for the callback only. Python `expansion_info` returns
an independent snapshot safe to retain; C++ callers must copy `getExpansionInfo()`
to retain metadata after the callback.

Parsing and register/unregister mutations require the primary server thread, and provider
callbacks execute there. `containsPlaceholders` / `contains_placeholders`, `isActive` /
`active`, and copied registration metadata queries may be called from any thread. The
introspection APIs are `isRegistered`, `getRegisteredIdentifiers`, and `getExpansions` in
C++, and `is_registered`, `registered_identifiers`, and `expansions` in Python. The
ordinary placeholder player may be null / `None`.

## Commands

| Command                         | Description                                                                  |
| ------------------------------- | ---------------------------------------------------------------------------- |
| `/papi parse <text>`          | Parse text using the sender when it is a player; otherwise use a null player |
| `/papi parse <target> <text>` | Parse text for a player name,`me`, or `--null` target                    |
| `/papi list`                  | List every registered identifier                                             |
| `/papi info <identifier>`     | Show one expansion's metadata                                                |

All require the `papi.command.papi` permission, which defaults to operators. `me` is
valid only for a player sender; invalid player names are rejected. Text may contain
spaces, and selectors are not supported.

## Requirements

Python: 3.11–3.14

Endstone: `==0.11.11.dev392` (API 0.12, verified build `33562961160`)

Supported packages: x86-64 Windows and Linux.

## Building from source

Requires CMake 3.29+, Ninja, Conan 2.30.0, and the Endstone toolchain: clang-cl 18+
with an x64 MSVC developer environment and Windows SDK on Windows, or Clang 18+ with
libc++ and libc++abi on Linux. Official release wheels use the repository-pinned
Clang 20 toolchain for reproducibility. Prepare the verified Endstone wheelhouse
above first and set `PIP_FIND_LINKS` to its absolute path before running a PEP 517
build (`$env:PIP_FIND_LINKS` in PowerShell, `export PIP_FIND_LINKS=...` in Bash).
Set `PIP_ONLY_BINARY=endstone` as well. These settings reach build isolation;
Linux repair and runtime installation must use the same official Endstone wheels.

```shell
python -m pip install "conan==2.30.0"
conan install . --build=missing
cmake --preset papi-dev
cmake --build --preset papi-dev
ctest --preset papi-dev --output-on-failure
python -m pytest -q
python -m pip install build
python -m build --wheel
```

## Contributing

CI administrators must create the `endstone-artifacts` GitHub environment and
restrict its deployment branches to `main` only. Store `ENDSTONE_ARTIFACTS_TOKEN`
only in that environment, with Actions read access to `EndstoneMC/endstone`.
Run **Stage Endstone wheelhouse** from `main`, then set repository variable
`ENDSTONE_WHEELHOUSE_RUN_ID` to that successful run's ID. Restage before its
artifact expires. These settings require administrator configuration; this
checkout does not create them. Pull request and build jobs download the same-repository
artifact and verify its pinned hashes without receiving the upstream token.
Fork repositories need their own trusted staging setup; missing staging fails explicitly.

Contributions are welcome! Feel free to fork the repository, improve the code, and
submit pull requests with your changes.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for
details.

PlaceholderAPI for Spigot is GPL-licensed. This project is an independent
implementation: its behavior was specified from observable inputs and outputs, and no
source was copied or translated.
