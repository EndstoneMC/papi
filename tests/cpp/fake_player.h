#pragma once

// A concrete endstone::Player for tests.
//
// PAPI hands provider callbacks borrowed Player references, so exercising relational
// dispatch and player cleanup needs real objects with real identity. Casting some
// other pointer to Player* would be undefined behavior, and on Windows the multiple
// inheritance in Player makes such a cast produce a wrong vtable outright.
//
// Only getName and getUniqueId carry meaning; every other member is a stub that never
// touches server state. The generated stubs mirror Endstone 0.11's Player hierarchy,
// so a signature change upstream surfaces here as a compile error rather than as a
// silently wrong test.

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <endstone/endstone.hpp>

namespace papi::testing {

class FakePlayer final : public endstone::Player {
public:
    FakePlayer(std::string name, const endstone::UUID id) : name_(std::move(name)), id_(id) {}

    [[nodiscard]] std::string getName() const override { return name_; }
    [[nodiscard]] endstone::UUID getUniqueId() const override { return id_; }

    // Endstone routes Player discovery through this hook rather than a cast.
    [[nodiscard]] endstone::Player *asPlayer() const override { return const_cast<FakePlayer *>(this); }

    bool isOp() const override { return false; }
    void setOp(bool value) override {}
    std::string getXuid() const override { return {}; }
    endstone::SocketAddress getAddress() const override { return {}; }
    void transfer(std::string host, int port) const override {}
    void kick(std::string message) const override {}
    bool performCommand(std::string command) const override { return false; }
    bool isSneaking() const override { return false; }
    void setSneaking(bool sneak) override {}
    bool isSprinting() const override { return false; }
    void setSprinting(bool sprinting) override {}
    void playSound(endstone::Location location, std::string sound, float volume, float pitch) override {}
    void stopSound(std::string sound) override {}
    void stopAllSounds() override {}
    void giveExp(int amount) override {}
    void giveExpLevels(int amount) override {}
    float getExpProgress() const override { return 0.0F; }
    void setExpProgress(float progress) override {}
    int getExpLevel() const override { return 0; }
    void setExpLevel(int level) override {}
    int getTotalExp() const override { return 0; }
    bool getAllowFlight() const override { return false; }
    void setAllowFlight(bool flight) override {}
    bool isFlying() const override { return false; }
    void setFlying(bool value) override {}
    float getFlySpeed() const override { return 0.0F; }
    void setFlySpeed(float value) const override {}
    float getWalkSpeed() const override { return 0.0F; }
    void setWalkSpeed(float value) const override {}
    endstone::Scoreboard &getScoreboard() const override
    {
        throw std::logic_error("FakePlayer::getScoreboard is not implemented");
    }
    void setScoreboard(endstone::Scoreboard &scoreboard) override {}
    void sendPopup(std::string message) const override {}
    void sendTip(std::string message) const override {}
    void sendToast(std::string title, std::string content) const override {}
    void sendTitle(std::string title, std::string subtitle) const override {}
    void sendTitle(std::string title, std::string subtitle, int fade_in, int stay, int fade_out) const override {}
    void resetTitle() const override {}
    void spawnParticle(std::string name, endstone::Location location) const override {}
    void spawnParticle(std::string name, float x, float y, float z) const override {}
    void spawnParticle(std::string name, endstone::Location location,
                       std::optional<std::string> molang_variables_json) const override
    {
    }
    void spawnParticle(std::string name, float x, float y, float z,
                       std::optional<std::string> molang_variables_json) const override
    {
    }
    std::chrono::milliseconds getPing() const override { return {}; }
    std::string getLocale() const override { return {}; }
    void updateCommands() const override {}
    endstone::PlayerInventory &getInventory() const override
    {
        throw std::logic_error("FakePlayer::getInventory is not implemented");
    }
    endstone::Inventory &getEnderChest() const override
    {
        throw std::logic_error("FakePlayer::getEnderChest is not implemented");
    }
    endstone::GameMode getGameMode() const override { return {}; }
    void setGameMode(endstone::GameMode mode) override {}
    std::string getDeviceOS() const override { return {}; }
    std::string getDeviceId() const override { return {}; }
    std::string getGameVersion() const override { return {}; }
    endstone::Skin getSkin() const override { throw std::logic_error("FakePlayer::getSkin is not implemented"); }
    void sendForm(FormVariant form) override {}
    void closeForm() override {}
    void sendPacket(int packet_id, std::string_view payload) const override {}
    void sendMap(endstone::MapView &map) override {}
    bool isGliding() const override { return false; }
    int getHealth() const override { return 0; }
    void setHealth(int health) const override {}
    int getMaxHealth() const override { return 0; }
    void setMaxHealth(int health) const override {}
    endstone::Mob *asMob() const override { return {}; }
    endstone::Item *asItem() const override { return {}; }
    std::string getType() const override { return {}; }
    std::uint64_t getRuntimeId() const override { return 0; }
    endstone::Location getLocation() const override
    {
        throw std::logic_error("FakePlayer::getLocation is not implemented");
    }
    endstone::Vector getVelocity() const override { return {}; }
    bool isOnGround() const override { return false; }
    bool isInWater() const override { return false; }
    bool isInLava() const override { return false; }
    endstone::Level &getLevel() const override { throw std::logic_error("FakePlayer::getLevel is not implemented"); }
    endstone::Dimension &getDimension() const override
    {
        throw std::logic_error("FakePlayer::getDimension is not implemented");
    }
    void setRotation(float yaw, float pitch) override {}
    bool teleport(const endstone::Location &location) override { return false; }
    bool teleport(const endstone::Actor &target) override { return false; }
    std::int64_t getId() const override { return 0; }
    void remove() override {}
    bool isDead() const override { return false; }
    bool isValid() const override { return false; }
    std::vector<std::string> getScoreboardTags() const override { return {}; }
    bool addScoreboardTag(std::string tag) const override { return false; }
    bool removeScoreboardTag(std::string tag) const override { return false; }
    bool isNameTagVisible() const override { return false; }
    void setNameTagVisible(bool visible) override {}
    bool isNameTagAlwaysVisible() const override { return false; }
    void setNameTagAlwaysVisible(bool visible) override {}
    std::string getNameTag() const override { return {}; }
    void setNameTag(std::string name) override {}
    std::string getScoreTag() const override { return {}; }
    void setScoreTag(std::string score) override {}
    endstone::ConsoleCommandSender *asConsole() const override { return {}; }
    endstone::BlockCommandSender *asBlock() const override { return {}; }
    endstone::Actor *asActor() const override { return {}; }
    void sendMessage(const endstone::Message &message) const override {}
    void sendErrorMessage(const endstone::Message &message) const override {}
    endstone::Server &getServer() const override { throw std::logic_error("FakePlayer::getServer is not implemented"); }
    endstone::PermissionLevel getPermissionLevel() const override { return {}; }
    bool isPermissionSet(std::string name) const override { return false; }
    bool isPermissionSet(const endstone::Permission &perm) const override { return false; }
    bool hasPermission(std::string name) const override { return false; }
    bool hasPermission(const endstone::Permission &perm) const override { return false; }
    endstone::PermissionAttachment *addAttachment(endstone::Plugin &plugin, const std::string &name,
                                                  bool value) override
    {
        return {};
    }
    endstone::PermissionAttachment *addAttachment(endstone::Plugin &plugin) override { return {}; }
    bool removeAttachment(endstone::PermissionAttachment &attachment) override { return false; }
    void recalculatePermissions() override {}
    std::unordered_set<endstone::PermissionAttachmentInfo *> getEffectivePermissions() const override { return {}; }
    endstone::CommandSender *asCommandSender() const override { return {}; }

private:
    std::string name_;
    endstone::UUID id_;
};

}  // namespace papi::testing
