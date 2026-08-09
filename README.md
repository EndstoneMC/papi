# Placeholder API for Endstone

A Placeholder API plugin for Endstone, inspired by the famous PAPI plugin for Spigot. This project provides both native
C++ support and Python bindings via the `endstone_papi` package for seamless integration.

> [!NOTE]
> This plugin is still under development and has not been released to PyPI. If you are interested in using this plugin,
> please check out the latest build from our GitHub Actions
> page: [Build Artifacts](https://github.com/endstone-essentials/papi/actions/workflows/build.yml).

## Install

- Download .whl file from [release](https://github.com/endstone-essentials/papi/releases) or [action](https://github.com/endstone-essentials/papi/actions/workflows/build.yml)
- Put it into `plugins` folder
- Restart the server, enjoy!

## Project Structure

```
endstone-essentials/papi/
├── endstone_papi/      # Python package (for Python plugins)
├── examples/
│   ├── cpp/            # C++ example plugin
│   └── python/         # Python example plugin
├── include/            # Header files (for C++ plugins)
├── CMakeLists.txt      # CMake build configuration
├── pyproject.toml      # Python package configuration
├── LICENSE             # License information
└── README.md           # This file
```

## Usages

### C++

> [!NOTE]
> you can obtain the `papi.h` header file packaged within the `.whl` file.

```c++
#include <endstone/endstone.hpp>
#include <endstone_papi/papi.h>

class JoinExample : public endstone::Plugin {
public:
    void onEnable() override
    {
        if (getServer().getPluginManager().getPlugin("papi")) {
            registerEvent(&JoinExample::onPlayerJoin, *this, endstone::EventPriority::Highest);
            papi_ = getServer().getServiceManager().load<papi::PlaceholderAPI>("PlaceholderAPI");
        }
        else {
            getLogger().warning("Could not find PlaceholderAPI! This plugin is required.");
            getServer().getPluginManager().disablePlugin(*this);
        }
    }

    void onPlayerJoin(endstone::PlayerJoinEvent &event)
    {
        std::string join_text = "{player_name} joined the server! Their game mode is {player_gamemode}";
        join_text = papi_->setPlaceholder(event.getPlayer(), join_text);
        event.setJoinMessage(join_text);
    }
    
private:
    std::shared_ptr<papi::PlaceholderAPI> papi_;
};
```

For the full codes, check our [C++ Example Plugin](examples/cpp).

### Python

```python
from endstone_papi import PlaceholderAPI
from endstone.plugin import Plugin
from endstone.event import event_handler, EventPriority, PlayerJoinEvent


class JoinExample(Plugin):
    api_version = "0.6"
    soft_depend = ["papi"]

    def on_enable(self):
        if self.server.plugin_manager.get_plugin("papi"):
            self.register_events(self)
            self.papi = self.server.service_manager.load("PlaceholderAPI")
        else:
            self.logger.warning("Could not find PlaceholderAPI! This plugin is required.")
            self.server.plugin_manager.disable_plugin(self)

    @event_handler(priority=EventPriority.HIGHEST)
    def on_player_join(self, event: PlayerJoinEvent):
        join_text = "{player_name} joined the server! Their game mode is {player_gamemode}"
        join_text = self.papi.set_placeholder(event.player, join_text)
        event.join_message = join_text
```

For the full codes, check our [Python Example Plugin](examples/python).

## Screenshots

![](assets/screenshot.jpg)

## Placeholder Structure

The structure of a placeholder is `{<identifier>|<params>}`. Field `params` are optional and can be used to provide additional
information to the placeholder. The format of `params` is decided by the processor of the placeholder indicated by `identifier`.
When `params` is missing, you can simply write the placeholder as `{identifier}`.

## Built-in Placeholders

There are a number of built-in placeholders that can be used once papi is installed:

- [x] `{x}` x coordinate
- [x] `{y}` y coordinate
- [x] `{z}` z coordinate
- [x] `{player_name}` player's name
- [x] `{dimension}` dimension name
- [x] `{dimension_id}` dimension id
- [x] `{ping}` player ping
- [x] `{date}` date
- [x] `{time}` time
- [x] `{datetime}` date and time
- [x] `{year}` year
- [x] `{month}` month
- [x] `{day}` day
- [x] `{hour}` hour
- [x] `{minute}` minute
- [x] `{second}` second
- [x] `{mc_version}` minecraft version
- [x] `{online}` online player count
- [x] `{max_online}` max online player count
- [x] `{address}` player's address
- [x] `{runtime_id}` player's runtime id
- [x] `{exp_level}` player's experience level
- [x] `{total_exp}` player's experience in total
- [x] `{exp_progress}` player's experience progress
- [x] `{game_mode}` player's game mode
- [x] `{xuid}` player's xuid
- [x] `{uuid}` player's uuid
- [x] `{device_os}` os name of player's current device
- [x] `{locale}` player's locale

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

Contributions are welcome! Feel free to fork the repository, improve the code, and submit pull requests with your
changes.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.