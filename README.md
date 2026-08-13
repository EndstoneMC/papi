# Placeholder API for Endstone

A PlaceholderAPI framework for [Endstone](https://github.com/EndstoneMC/endstone),
inspired by the PlaceholderAPI plugin for Spigot. C++ and Python plugins are equal
providers: both implement the same expansion contract and share one registry, so a
consumer never needs to know which language a placeholder came from.

## Install

- Download the official `.whl` from [GitHub Releases](https://github.com/EndstoneMC/papi/releases)
- Put it in the `plugins` folder
- Restart the server

## Placeholder syntax

A placeholder is written `{identifier_params}`.

The **first underscore** separates the identifier from the parameters. The identifier is
ASCII-lowercased and matched case-insensitively; the parameters are passed to the
expansion exactly as written, including case, spaces, and an empty value.

| Input | Identifier | Params | Notes |
|---|---|---|---|
| `{player_name}` | `player` | `name` | the ordinary form |
| `{PLAYER_NaMe}` | `player` | `NaMe` | identifier lowercased, params preserved |
| `{player_name_first}` | `player` | `name_first` | only the first underscore splits |
| `{player_}` | `player` | *(empty)* | dispatched with empty params |
| `{player}` | — | — | no underscore, so it stays literal |

Anything that cannot be resolved is left exactly as written: malformed syntax, an unknown
identifier, an expansion that returns no value, or an expansion exception. An empty
string is a valid replacement. Replacement text is never re-scanned, so parsing is
one-pass and nonrecursive.

`{rel_identifier_params}` is a separate, relational form handled only by
`setRelationalPlaceholders` / `set_relational_placeholders`, and only by expansions that
opt in to the relational callback.

Identifiers must match `[A-Za-z0-9][A-Za-z0-9.-]*`. Registration is case-insensitive,
so identifiers that differ only by case collide. Underscore and colon are invalid in an
identifier.

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

    [[nodiscard]] std::optional<std::string> onRequest(const endstone::OfflinePlayer *player,
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
    api_version = "0.11"
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

Parsing and register/unregister mutations require the primary server thread, and provider
callbacks execute there. `containsPlaceholders` / `contains_placeholders`, `isActive` /
`active`, and copied registration metadata queries may be called from any thread. The
introspection APIs are `isRegistered`, `getRegisteredIdentifiers`, and `getExpansions` in
C++, and `is_registered`, `registered_identifiers`, and `expansions` in Python. The
ordinary placeholder player may be null / `None`.

## Commands

| Command | Description |
|---|---|
| `/papi parse <text>` | Parse text using the sender when it is a player; otherwise use a null player |
| `/papi parse <target> <text>` | Parse text for a player name, `me`, or `--null` target |
| `/papi list` | List every registered identifier |
| `/papi info <identifier>` | Show one expansion's metadata |

All require the `papi.command.papi` permission, which defaults to operators. `me` is
valid only for a player sender; invalid player names are rejected. Text may contain
spaces, and selectors are not supported.

## Requirements

Python: 3.10+

Endstone: `>=0.11.8,<0.12` (API 0.11)

Supported packages: x86-64 Windows and Linux.

## Building from source

Requires CMake 3.29+, Ninja, Conan 2.30.0, and the Endstone toolchain: clang-cl 20
with an x64 MSVC developer environment and Windows SDK on Windows, or Clang 20 with
libc++ and libc++abi on Linux.

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

Contributions are welcome! Feel free to fork the repository, improve the code, and
submit pull requests with your changes.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for
details.

PlaceholderAPI for Spigot is GPL-licensed. This project is an independent
implementation: its behavior was specified from observable inputs and outputs, and no
source was copied or translated.
