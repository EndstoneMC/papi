# Placeholder API for Endstone

A PlaceholderAPI framework for [Endstone](https://github.com/EndstoneMC/endstone),
inspired by the PlaceholderAPI plugin for Spigot. C++ and Python plugins are equal
providers: both implement the same expansion contract and share one registry, so a
consumer never needs to know which language a placeholder came from.

> [!NOTE]
> This plugin is under development and has not been released to PyPI. Builds are
> available from
> [GitHub Actions](https://github.com/EndstoneMC/papi/actions/workflows/build.yml).

## Install

- Download the `.whl` from [releases](https://github.com/EndstoneMC/papi/releases) or
  [actions](https://github.com/EndstoneMC/papi/actions/workflows/build.yml)
- Put it in the `plugins` folder
- Restart the server

## Placeholder syntax

A placeholder is written `{identifier_params}`.

The **first underscore** separates the identifier from the parameters. The identifier is
matched case-insensitively; the parameters are passed to the expansion exactly as
written, including case and spaces.

| Input | Identifier | Params | Notes |
|---|---|---|---|
| `{player_name}` | `player` | `name` | the ordinary form |
| `{PLAYER_NaMe}` | `player` | `NaMe` | identifier lowercased, params preserved |
| `{player_name_first}` | `player` | `name_first` | only the first underscore splits |
| `{player_}` | `player` | *(empty)* | dispatched with empty params |
| `{player}` | — | — | no underscore, so it stays literal |

Anything that cannot be resolved is left exactly as written: an unknown identifier, an
expansion that declines, an expansion that fails, or malformed braces. Replacement text
is never re-scanned, so a value that itself contains `{...}` is passed through verbatim.

`{rel_identifier_params}` is a separate, relational form handled only by
`set_relational_placeholders`, and only by expansions that declare relational support.

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
        service = self.server.service_manager.load("PlaceholderAPI")
        if not isinstance(service, PlaceholderAPI) or not service.active:
            return
        service.register_expansion(self, NameExpansion())
```

For the full code, see the [Python example plugin](examples/python).

### Lifecycle

PAPI owns a registered expansion until it is unregistered, so a provider does not have
to keep its own reference. Expansions are removed automatically when their owning plugin
is disabled, or when a plugin they declared as `required_plugin` is disabled. Calling
`unregister_expansions(self)` from `on_disable` is allowed and simply makes that
explicit.

Parsing and registration must happen on the server thread, because they call into
provider code. A retained service reference stays safe after PAPI is disabled: it
becomes inert, so `active` turns False, parsing returns its input unchanged, and queries
come back empty.

## Commands

| Command | Description |
|---|---|
| `/papi parse [target] <text>` | Parse text; target may be a player name, `me`, or `--null` |
| `/papi list` | List every registered identifier |
| `/papi info <identifier>` | Show one expansion's metadata |

All require the `papi.command.papi` permission, which defaults to operators.

## Migrating from 0.0.1

This release is a rewrite; 0.0.1 plugins need changes.

| 0.0.1 | Now |
|---|---|
| `{identifier\|params}` | `{identifier_params}` |
| built-in placeholders (`{x}`, `{ping}`, `{date}`, …) | provide them from your own expansion |
| `PlaceholderAPI(plugin)` in Python | load the service from the service manager |
| subclassing `PlaceholderAPI` | subclass `PlaceholderExpansion` instead |
| `register_placeholder(plugin, id, processor)` | `register_expansion(plugin, expansion)` |
| duplicate id became `plugin:identifier` | duplicate registration fails |
| `setPlaceholders(Player*, text)` | `setPlaceholders(OfflinePlayer*, text)`, which accepts null |

The C++ `registerPlaceholder` overload and `getPlaceholderPattern` remain for one
release, marked deprecated. The processor form cannot express "unresolved" — every
return value is used verbatim — and only ever receives an online player, so prefer an
expansion.

Identifiers are now validated: they must match `[A-Za-z0-9][A-Za-z0-9.-]*`. Underscore
is the parameter separator and colon belonged to the removed duplicate-namespace
behavior, so neither is allowed in an identifier.

## Requirement

Python: 3.10+

Endstone: 0.11.8 (API 0.11)

## Building from source

Requires CMake 3.29+, Ninja, Conan 2, and the Endstone toolchain: `clang-cl` on
Windows (from an x64 MSVC developer environment) or Clang with libc++ on Linux.

```shell
python -m pip install "conan>=2,<3"
conan install . --build=missing
cmake --preset papi-dev
cmake --build --preset papi-dev
ctest --preset papi-dev --output-on-failure
python -m pytest -q
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
