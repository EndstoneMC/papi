#pragma once

// Concrete Player identity for relational and cleanup tests.

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

#include <endstone/endstone.hpp>

namespace papi::testing {

class FakePlayer final : public endstone::Player {
private:
    class FakeActorType final : public endstone::ActorType {
    public:
        [[nodiscard]] endstone::ActorTypeId getId() const override { return endstone::ActorType::Player; }
        [[nodiscard]] std::string getTranslationKey() const override { return "entity.minecraft.player.name"; }
    };

public:
    FakePlayer(std::string name, const endstone::UUID id) : name_(std::move(name)), id_(id) {}

    [[nodiscard]] endstone::ClassInfo getClassInfo() const override { return typeid(FakePlayer); }

    [[nodiscard]] bool isInstanceOf(endstone::ClassInfo target) const override
    {
        return target == typeid(FakePlayer) || target == typeid(endstone::Player) || target == typeid(endstone::Mob) ||
               target == typeid(endstone::Actor) || target == typeid(endstone::CommandSender) ||
               target == typeid(endstone::Permissible) || target == typeid(endstone::Object);
    }

    [[nodiscard]] std::string getName() const override { return name_; }
    [[nodiscard]] endstone::UUID getUniqueId() const override { return id_; }

    [[nodiscard]] const endstone::ActorType &getType() const override
    {
        static const FakeActorType type;
        return type;
    }

    [[nodiscard]] std::uint64_t getRuntimeId() const override { return 0; }
    [[nodiscard]] endstone::Location getLocation() const override
    {
        throw std::logic_error("FakePlayer::getLocation is not implemented");
    }
    [[nodiscard]] endstone::Vector getVelocity() const override { return {}; }
    [[nodiscard]] bool isOnGround() const override { return false; }
    [[nodiscard]] bool isInWater() const override { return false; }
    [[nodiscard]] bool isInLava() const override { return false; }
    [[nodiscard]] endstone::Level &getLevel() const override
    {
        throw std::logic_error("FakePlayer::getLevel is not implemented");
    }
    [[nodiscard]] endstone::NotNull<endstone::Dimension> getDimension() const override
    {
        throw std::logic_error("FakePlayer::getDimension is not implemented");
    }
    void setRotation(float yaw, float pitch) override {}
    bool teleport(const endstone::Location &location) override { return false; }
    bool teleport(const endstone::NotNull<endstone::Actor> &target) override { return false; }
    [[nodiscard]] std::int64_t getId() const override { return 0; }
    void remove() override {}
    [[nodiscard]] bool isDead() const override { return false; }
    [[nodiscard]] bool isValid() const override { return false; }
    [[nodiscard]] std::vector<std::string> getScoreboardTags() const override { return {}; }
    [[nodiscard]] bool addScoreboardTag(std::string tag) const override { return false; }
    [[nodiscard]] bool removeScoreboardTag(std::string tag) const override { return false; }
    [[nodiscard]] bool isNameTagVisible() const override { return false; }
    void setNameTagVisible(bool visible) override {}
    [[nodiscard]] bool isNameTagAlwaysVisible() const override { return false; }
    void setNameTagAlwaysVisible(bool visible) override {}
    [[nodiscard]] std::string getNameTag() const override { return {}; }
    void setNameTag(std::string name) override {}
    [[nodiscard]] std::string getScoreTag() const override { return {}; }
    void setScoreTag(std::string score) override {}

    [[nodiscard]] bool isGliding() const override { return false; }
    [[nodiscard]] bool isSwimming() const override { return false; }
    [[nodiscard]] int getHealth() const override { return 0; }
    void setHealth(int health) const override {}
    [[nodiscard]] int getMaxHealth() const override { return 0; }
    void setMaxHealth(int health) const override {}
    [[nodiscard]] bool hasAttribute(endstone::AttributeId id) const override { return false; }
    [[nodiscard]] endstone::Nullable<endstone::AttributeInstance> getAttribute(endstone::AttributeId id) override
    {
        return nullptr;
    }
    [[nodiscard]] std::vector<endstone::NotNull<endstone::AttributeInstance>> getAttributes() override { return {}; }
    void addEffect(const endstone::Effect &effect) override {}
    void removeEffect(endstone::EffectId type) override {}
    [[nodiscard]] bool hasEffect(endstone::EffectId type) const override { return false; }
    [[nodiscard]] std::optional<endstone::Effect> getEffect(endstone::EffectId type) const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<endstone::Effect> getActiveEffects() const override { return {}; }

    [[nodiscard]] bool isOp() const override { return false; }
    void setOp(bool value) override {}
    [[nodiscard]] std::string getXuid() const override { return {}; }
    [[nodiscard]] const endstone::SocketAddress &getAddress() const override
    {
        static const endstone::SocketAddress address{};
        return address;
    }
    void transfer(std::string host, int port) const override {}
    void kick(std::string message) const override {}
    bool performCommand(std::string command) const override { return false; }
    [[nodiscard]] std::optional<endstone::Location> getRespawnLocation() const override { return std::nullopt; }
    void setRespawnLocation(std::optional<endstone::Location> location) override {}
    void sendBlockUpdate(const endstone::Location &location,
                         const endstone::BlockActorState &block_actor_state) override
    {
    }
    void hideActor(endstone::Plugin &plugin, endstone::Actor &actor) override {}
    void showActor(endstone::Plugin &plugin, endstone::Actor &actor) override {}
    [[nodiscard]] bool canSee(const endstone::Actor &actor) const override { return false; }
    [[nodiscard]] bool canSee(const endstone::Player &player) const override { return false; }
    void sendBlockChange(const endstone::Location &location, const endstone::BlockData &block) override {}
    void openSign(const endstone::Sign &sign, endstone::Sign::Side side) override {}
    void openVirtualSign(const endstone::Location &location, endstone::Sign::Side side) override {}
    [[nodiscard]] bool isSneaking() const override { return false; }
    void setSneaking(bool sneak) override {}
    [[nodiscard]] bool isSprinting() const override { return false; }
    void setSprinting(bool sprinting) override {}
    [[nodiscard]] bool isCrawling() const override { return false; }
    void playSound(endstone::Location location, std::string sound, float volume, float pitch) override {}
    void stopSound(std::string sound) override {}
    void stopAllSounds() override {}
    void giveExp(int amount) override {}
    void giveExpLevels(int amount) override {}
    [[nodiscard]] float getExpProgress() const override { return 0.0F; }
    void setExpProgress(float progress) override {}
    [[nodiscard]] int getExpLevel() const override { return 0; }
    void setExpLevel(int level) override {}
    [[nodiscard]] int getTotalExp() const override { return 0; }
    [[nodiscard]] bool getAllowFlight() const override { return false; }
    void setAllowFlight(bool flight) override {}
    [[nodiscard]] bool isFlying() const override { return false; }
    void setFlying(bool value) override {}
    [[nodiscard]] float getFlySpeed() const override { return 0.0F; }
    void setFlySpeed(float value) const override {}
    [[nodiscard]] float getWalkSpeed() const override { return 0.0F; }
    void setWalkSpeed(float value) const override {}
    [[nodiscard]] endstone::NotNull<endstone::Scoreboard> getScoreboard() const override
    {
        throw std::logic_error("FakePlayer::getScoreboard is not implemented");
    }
    void setScoreboard(endstone::NotNull<endstone::Scoreboard> scoreboard) override {}
    void sendActionBar(std::string message) const override {}
    void sendPopup(std::string message) const override {}
    void sendTip(std::string message) const override {}
    void sendToast(std::string title, std::string content) const override {}
    void sendTitle(std::string title, std::string subtitle) const override {}
    void sendTitle(std::string title, std::string subtitle, int fade_in, int stay, int fade_out) const override {}
    void resetTitle() const override {}
    void spawnParticle(std::string name, endstone::Location location) const override {}
    void spawnParticle(std::string name, float x, float y, float z) const override {}
    void spawnParticle(std::string name, endstone::Location location,
                       std::optional<endstone::JsonObject> molang_variables) const override
    {
    }
    void spawnParticle(std::string name, float x, float y, float z,
                       std::optional<endstone::JsonObject> molang_variables) const override
    {
    }
    [[nodiscard]] std::chrono::milliseconds getPing() const override { return {}; }
    [[nodiscard]] std::string getLocale() const override { return {}; }
    void updateCommands() const override {}
    [[nodiscard]] endstone::PlayerInventory &getInventory() const override
    {
        throw std::logic_error("FakePlayer::getInventory is not implemented");
    }
    [[nodiscard]] endstone::Inventory &getEnderChest() const override
    {
        throw std::logic_error("FakePlayer::getEnderChest is not implemented");
    }
    [[nodiscard]] endstone::GameMode getGameMode() const override { return {}; }
    void setGameMode(endstone::GameMode mode) override {}
    [[nodiscard]] std::string getDeviceOS() const override { return {}; }
    [[nodiscard]] std::string getDeviceId() const override { return {}; }
    [[nodiscard]] std::string getGameVersion() const override { return {}; }
    [[nodiscard]] endstone::Skin getSkin() const override
    {
        throw std::logic_error("FakePlayer::getSkin is not implemented");
    }
    void sendForm(FormVariant form) override {}
    void closeForm() override {}
    void sendPacket(int packet_id, std::string_view payload) const override {}
    void sendMap(endstone::MapView &map) override {}
    [[nodiscard]] endstone::AbilityValue _getAbility(endstone::Identifier<endstone::Ability> ability) const override
    {
        return false;
    }
    bool _setAbility(endstone::Identifier<endstone::Ability> ability, endstone::AbilityValue value) override
    {
        return false;
    }

    void sendMessage(const endstone::Message &message) const override {}
    void sendErrorMessage(const endstone::Message &message) const override {}
    [[nodiscard]] endstone::Server &getServer() const override
    {
        throw std::logic_error("FakePlayer::getServer is not implemented");
    }
    [[nodiscard]] endstone::PermissionLevel getPermissionLevel() const override { return {}; }
    [[nodiscard]] bool isPermissionSet(std::string name) const override { return false; }
    [[nodiscard]] bool isPermissionSet(const endstone::NotNull<endstone::Permission> &perm) const override
    {
        return false;
    }
    [[nodiscard]] bool hasPermission(std::string name) const override { return false; }
    [[nodiscard]] bool hasPermission(const endstone::NotNull<endstone::Permission> &perm) const override
    {
        return false;
    }
    [[nodiscard]] endstone::NotNull<endstone::PermissionAttachment> addAttachment(endstone::Plugin &plugin,
                                                                                  const std::string &name,
                                                                                  bool value) override
    {
        throw std::logic_error("FakePlayer::addAttachment is not implemented");
    }
    [[nodiscard]] endstone::NotNull<endstone::PermissionAttachment> addAttachment(endstone::Plugin &plugin) override
    {
        throw std::logic_error("FakePlayer::addAttachment is not implemented");
    }
    bool removeAttachment(const endstone::NotNull<endstone::PermissionAttachment> &attachment) override
    {
        return false;
    }
    void recalculatePermissions() override {}
    [[nodiscard]] std::unordered_set<endstone::NotNull<endstone::PermissionAttachmentInfo>> getEffectivePermissions()
        const override
    {
        return {};
    }

private:
    std::string name_;
    endstone::UUID id_;
};

}  // namespace papi::testing
