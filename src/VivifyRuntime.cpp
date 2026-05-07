#include "VivifyRuntime.hpp"
#include "main.hpp"
#include "VivifyHandlers.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <filesystem>
#include <fstream>
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapCallbacksController.hpp"
#include "GlobalNamespace/BpmController.hpp"
#include "GlobalNamespace/StaticBeatmapObjectSpawnMovementData.hpp"
#include "UnityEngine/Animator.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/CameraClearFlags.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/DepthTextureMode.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/FogMode.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Graphics.hpp"
#include "UnityEngine/Light.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/QualitySettings.hpp"
#include "UnityEngine/RenderSettings.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/RenderTextureDescriptor.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/Rendering/AmbientMode.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector4.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Rigidbody.hpp"
#include "UnityEngine/Collider.hpp"
#include "UnityEngine/Video/VideoPlayer.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/SaberType.hpp"
#include "GlobalNamespace/NoteController.hpp"
#include "GlobalNamespace/GameNoteController.hpp"
#include "GlobalNamespace/BombNoteController.hpp"
#include "GlobalNamespace/BurstSliderGameNoteController.hpp"
#include "GlobalNamespace/NoteData.hpp"
#include "GlobalNamespace/NoteVisualModifierType.hpp"
#include "GlobalNamespace/MaterialPropertyBlockController.hpp"
#include "GlobalNamespace/NoteSpawnData.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "custom-json-data/shared/CustomBeatmapData.h"
#include "custom-json-data/shared/CustomEventData.h"
#include "custom-types/shared/macros.hpp"
#include "paper2_scotland2/shared/logger.hpp"
#include "scotland2/shared/modloader.h"
#include "songcore/shared/Capabilities.hpp"
#include "songcore/shared/SongCore.hpp"
#include "tracks/shared/Animation/Easings.h"
#include "tracks/shared/Animation/GameObjectTrackController.hpp"
#include "tracks/shared/Animation/PointDefinition.h"
#include "tracks/shared/Animation/TransformData.hpp"
#include "tracks/shared/AssociatedData.h"
#include "tracks/shared/Constants.h"
#include "tracks/shared/StaticHolders.hpp"
#include "web-utils/shared/WebUtils.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "metacore/shared/game.hpp"
#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
using namespace std::string_view_literals;
DECLARE_CLASS_CODEGEN(Vivify, RuntimeBehaviour, UnityEngine::MonoBehaviour) {
  DECLARE_DEFAULT_CTOR();
  DECLARE_SIMPLE_DTOR();
  DECLARE_INSTANCE_METHOD(void, Update);
  DECLARE_INSTANCE_METHOD(void, OnDestroy);
};
#include "VivifyCameraApplier.hpp"
DEFINE_TYPE(Vivify, RuntimeBehaviour);
DEFINE_TYPE(Vivify, CameraApplier);
namespace Vivify {
namespace {
constexpr std::string_view kCapability = "Vivify"sv;
constexpr std::string_view kBundleFile = "bundleAndroid2021.vivify"sv;
constexpr std::string_view kInstantiatePrefabEvent = "InstantiatePrefab"sv;
constexpr std::string_view kDestroyObjectEvent = "DestroyObject"sv;
constexpr std::string_view kSetMaterialPropertyEvent = "SetMaterialProperty"sv;
constexpr std::string_view kSetAnimatorPropertyEvent = "SetAnimatorProperty"sv;
constexpr std::string_view kSetGlobalPropertyEvent = "SetGlobalProperty"sv;
constexpr std::string_view kAssignObjectPrefabEvent = "AssignObjectPrefab"sv;
constexpr std::string_view kBlitEvent = "Blit"sv;
constexpr std::string_view kCreateCameraEvent = "CreateCamera"sv;
constexpr std::string_view kCreateScreenTextureEvent = "CreateScreenTexture"sv;
constexpr std::string_view kSetCameraPropertyEvent = "SetCameraProperty"sv;
constexpr std::string_view kSetRenderingSettingsEvent = "SetRenderingSettings"sv;
enum class MaterialPropertyKind {
  Unsupported,
  Texture,
  Color,
  Float,
  Int,
  Vector,
  Keyword,
};
enum class AnimatorPropertyKind {
  Unsupported,
  Bool,
  Float,
  Integer,
  Trigger,
};
using MaterialValue = std::variant<std::monostate, std::string, bool, float, int, PointDefinitionW>;
using AnimatorValue = std::variant<std::monostate, bool, float, int, PointDefinitionW>;
using SavedGlobalValue = std::variant<UnityEngine::Texture*, UnityEngine::Color, float, UnityEngine::Vector4>;
struct MaterialPropertyChange {
  std::variant<int, std::string> id;
  MaterialPropertyKind kind = MaterialPropertyKind::Unsupported;
  MaterialValue value;
};
struct AnimatorPropertyChange {
  std::string name;
  AnimatorPropertyKind kind = AnimatorPropertyKind::Unsupported;
  AnimatorValue value;
};
struct InstantiatePrefabData {
  std::string asset;
  std::optional<std::string> id;
  Tracks::TransformData transformData;
  std::vector<TrackW> tracks;
  UnityEngine::GameObject* instance = nullptr;
};
struct LivePrefab {
  UnityEngine::GameObject* gameObject = nullptr;
  std::vector<TrackW> tracks;
  std::vector<UnityEngine::Animator*> animators;
};
struct ActiveMaterialAnimation {
  UnityEngine::Material* material = nullptr;
  std::vector<MaterialPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  Functions easing = Functions::EaseLinear;
};
struct ActiveGlobalAnimation {
  std::vector<MaterialPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  Functions easing = Functions::EaseLinear;
};
struct ActiveAnimatorAnimation {
  std::string prefabId;
  std::vector<AnimatorPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  Functions easing = Functions::EaseLinear;
};
enum class PostProcessingOrder { BeforeMainEffect, AfterMainEffect };
struct BlitMaterialData {
  UnityEngine::Material* material = nullptr;
  int priority = 0;
  std::string source;
  std::vector<std::string> targets;
  int pass = -1;
  std::optional<int> frame;
  bool operator<(BlitMaterialData const& o) const { return priority < o.priority; }
};
struct ActiveBlitEffect {
  BlitMaterialData data;
  float expireTime = 0.0f;
};
struct DeclaredTextureData {
  std::string name;
  int propertyId = 0;
  float xRatio = 1.0f;
  float yRatio = 1.0f;
  std::optional<int> width;
  std::optional<int> height;
  std::optional<UnityEngine::RenderTextureFormat> format;
  std::optional<UnityEngine::FilterMode> filterMode;
  UnityEngine::RenderTexture* texture = nullptr;
};
struct CameraPropertyData {
  std::optional<UnityEngine::DepthTextureMode> depthTextureMode;
  std::optional<UnityEngine::CameraClearFlags> clearFlags;
  std::optional<UnityEngine::Color> backgroundColor;
  std::optional<bool> bloomPrePass;
  std::optional<bool> mainEffect;
};
struct SecondaryCameraData {
  std::string name;
  std::optional<std::string> textureName;
  std::optional<std::string> depthTextureName;
  std::optional<int> texturePropertyId;
  std::optional<int> depthTexturePropertyId;
  CameraPropertyData properties;
  UnityEngine::Camera* camera = nullptr;
  UnityEngine::RenderTexture* colorRT = nullptr;
  UnityEngine::RenderTexture* depthRT = nullptr;
};
enum class RenderSettingKind { Float, Color, Bool, Int, Enum, Material, Light };
struct RenderSettingValue {
  std::string name;
  RenderSettingKind kind = RenderSettingKind::Float;
  std::variant<std::monostate, float, UnityEngine::Color, int, bool, std::string, PointDefinitionW> value;
};
struct SavedRenderSetting {
  std::string name;
  RenderSettingKind kind;
  std::variant<float, UnityEngine::Color, int, bool, UnityEngine::Material*, UnityEngine::Light*> saved;
};
struct ActiveRenderSettingAnimation {
  std::vector<RenderSettingValue> settings;
  float startTime = 0.0f;
  float duration = 0.0f;
  Functions easing = Functions::EaseLinear;
};
struct AssignedPrefabInfo {
  std::string asset;
  std::vector<TrackW> tracks;
  std::string objectType;
  std::optional<int> saberType;
  bool showArrow = false; // if true, preserve the original note arrow renderer
};
bool IsVivifyEvent(std::string_view type) {
  return type == kInstantiatePrefabEvent || type == kDestroyObjectEvent || type == kSetMaterialPropertyEvent ||
         type == kSetAnimatorPropertyEvent || type == kSetGlobalPropertyEvent || type == kAssignObjectPrefabEvent ||
         type == kBlitEvent || type == kCreateCameraEvent || type == kCreateScreenTextureEvent ||
         type == kSetCameraPropertyEvent || type == kSetRenderingSettingsEvent;
}
bool IsSupportedEvent(std::string_view type) {
  return IsVivifyEvent(type);
}
std::string NormalizeAssetKey(std::string_view input) {
  std::string key(input);
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return key;
}
std::string JoinPath(std::string_view left, std::string_view right) {
  std::string result(left);
  if (!result.empty() && result.back() != '/' && result.back() != '\\') {
    result.push_back('/');
  }
  result.append(right);
  return result;
}
std::optional<std::string_view> ReadStringView(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd() || !member->value.IsString()) {
    return std::nullopt;
  }
  return member->value.GetString();
}
std::optional<float> ReadFloat(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  if (member->value.IsNumber()) {
    return member->value.GetFloat();
  }
  if (member->value.IsBool()) {
    return member->value.GetBool() ? 1.0f : 0.0f;
  }
  return std::nullopt;
}
std::optional<int> ReadInt(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  if (member->value.IsInt()) {
    return member->value.GetInt();
  }
  if (member->value.IsNumber()) {
    return static_cast<int>(member->value.GetFloat());
  }
  return std::nullopt;
}
std::optional<bool> ReadBool(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  auto const& value = member->value;
  if (value.IsBool()) {
    return value.GetBool();
  }
  if (value.IsNumber()) {
    return value.GetFloat() != 0.0f;
  }
  if (value.IsString()) {
    return std::string_view(value.GetString()) == "true"sv;
  }
  return std::nullopt;
}
rapidjson::Value const* ReadValuePtr(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  return member == object.MemberEnd() ? nullptr : &member->value;
}
std::vector<std::string> ReadStringListOrSingle(rapidjson::Value const& object, std::string_view key) {
  std::vector<std::string> result;
  auto value = ReadValuePtr(object, key);
  if (value == nullptr) {
    return result;
  }
  if (value->IsString()) {
    result.emplace_back(value->GetString());
    return result;
  }
  if (!value->IsArray()) {
    return result;
  }
  result.reserve(value->Size());
  for (auto const& entry : value->GetArray()) {
    if (entry.IsString()) {
      result.emplace_back(entry.GetString());
    }
  }
  return result;
}
Functions ParseEasing(std::string_view easing) {
  if (easing == "easeStep"sv) return Functions::EaseStep;
  if (easing == "easeInQuad"sv) return Functions::EaseInQuad;
  if (easing == "easeOutQuad"sv) return Functions::EaseOutQuad;
  if (easing == "easeInOutQuad"sv) return Functions::EaseInOutQuad;
  if (easing == "easeInCubic"sv) return Functions::EaseInCubic;
  if (easing == "easeOutCubic"sv) return Functions::EaseOutCubic;
  if (easing == "easeInOutCubic"sv) return Functions::EaseInOutCubic;
  if (easing == "easeInQuart"sv) return Functions::EaseInQuart;
  if (easing == "easeOutQuart"sv) return Functions::EaseOutQuart;
  if (easing == "easeInOutQuart"sv) return Functions::EaseInOutQuart;
  if (easing == "easeInQuint"sv) return Functions::EaseInQuint;
  if (easing == "easeOutQuint"sv) return Functions::EaseOutQuint;
  if (easing == "easeInOutQuint"sv) return Functions::EaseInOutQuint;
  if (easing == "easeInSine"sv) return Functions::EaseInSine;
  if (easing == "easeOutSine"sv) return Functions::EaseOutSine;
  if (easing == "easeInOutSine"sv) return Functions::EaseInOutSine;
  if (easing == "easeInCirc"sv) return Functions::EaseInCirc;
  if (easing == "easeOutCirc"sv) return Functions::EaseOutCirc;
  if (easing == "easeInOutCirc"sv) return Functions::EaseInOutCirc;
  if (easing == "easeInExpo"sv) return Functions::EaseInExpo;
  if (easing == "easeOutExpo"sv) return Functions::EaseOutExpo;
  if (easing == "easeInOutExpo"sv) return Functions::EaseInOutExpo;
  if (easing == "easeInElastic"sv) return Functions::EaseInElastic;
  if (easing == "easeOutElastic"sv) return Functions::EaseOutElastic;
  if (easing == "easeInOutElastic"sv) return Functions::EaseInOutElastic;
  if (easing == "easeInBack"sv) return Functions::EaseInBack;
  if (easing == "easeOutBack"sv) return Functions::EaseOutBack;
  if (easing == "easeInOutBack"sv) return Functions::EaseInOutBack;
  if (easing == "easeInBounce"sv) return Functions::EaseInBounce;
  if (easing == "easeOutBounce"sv) return Functions::EaseOutBounce;
  if (easing == "easeInOutBounce"sv) return Functions::EaseInOutBounce;
  return Functions::EaseLinear;
}
MaterialPropertyKind ParseMaterialPropertyKind(std::string_view kind) {
  if (kind == "Texture"sv) return MaterialPropertyKind::Texture;
  if (kind == "Color"sv) return MaterialPropertyKind::Color;
  if (kind == "Float"sv) return MaterialPropertyKind::Float;
  if (kind == "Int"sv) return MaterialPropertyKind::Int;
  if (kind == "Vector"sv) return MaterialPropertyKind::Vector;
  if (kind == "Keyword"sv) return MaterialPropertyKind::Keyword;
  return MaterialPropertyKind::Unsupported;
}
AnimatorPropertyKind ParseAnimatorPropertyKind(std::string_view kind) {
  if (kind == "Bool"sv) return AnimatorPropertyKind::Bool;
  if (kind == "Float"sv) return AnimatorPropertyKind::Float;
  if (kind == "Integer"sv) return AnimatorPropertyKind::Integer;
  if (kind == "Trigger"sv) return AnimatorPropertyKind::Trigger;
  return AnimatorPropertyKind::Unsupported;
}
UnityEngine::Color VectorToColor(NEVector::Vector4 value) {
  return UnityEngine::Color(value.x, value.y, value.z, value.w);
}
UnityEngine::Vector4 ToUnityVector(NEVector::Vector4 value) {
  return UnityEngine::Vector4(value.x, value.y, value.z, value.w);
}
class Runtime {
public:
  static Runtime& Instance() {
    static Runtime runtime;
    return runtime;
  }
  CustomJSONData::CustomBeatmapData* GetCurrentBeatmapData() const { return _currentBeatmapData; }
  bool IsResetting() const { return _isResetting; }
  AssignedPrefabInfo* FindAssignedPrefab(std::string_view objectType, GlobalNamespace::NoteData* noteData) {
    if (noteData == nullptr) return nullptr;
    auto* customNoteData = il2cpp_utils::cast<CustomJSONData::CustomNoteData>(noteData);
    auto& ad = TracksAD::getAD(customNoteData->customData);
    if (ad.tracks.empty()) return nullptr;
    for (auto& info : _assignedPrefabs) {
      if (info.objectType != objectType) continue;
      for (auto& t : ad.tracks) {
        for (auto& it : info.tracks) {
          if (t == it) return &info;
        }
      }
    }
    return nullptr;
  }
  AssignedPrefabInfo* FindAssignedSaberPrefab(int type) {
    for (auto& info : _assignedPrefabs) {
      if (info.objectType == "saber" && info.saberType.has_value() && info.saberType.value() == type) {
        return &info;
      }
    }
    return nullptr;
  }
  void CleanCustomObject(UnityEngine::GameObject* go) {
    if (go == nullptr || !UnityEngine::Object::op_Implicit_bool(go)) return;
    auto rigidbodies = go->GetComponentsInChildren<UnityEngine::Rigidbody*>(true);
    for (int i = 0; i < rigidbodies.size(); i++) {
      rigidbodies[i]->set_isKinematic(true);
      rigidbodies[i]->set_useGravity(false);
    }
    auto colliders = go->GetComponentsInChildren<UnityEngine::Collider*>(true);
    for (int i = 0; i < colliders.size(); i++) {
      colliders[i]->set_enabled(false);
    }
    auto videoArray = go->GetComponentsInChildren<UnityEngine::Video::VideoPlayer*>(true);
    for (int i = 0; i < videoArray.size(); i++) {
      if (videoArray[i] != nullptr) {
        _videoPlayers.emplace_back(videoArray[i]);
      }
    }
  }
  void ReplaceNoteVisuals(GlobalNamespace::NoteController* noteController, AssignedPrefabInfo* info) {
    if (noteController == nullptr || info == nullptr) return;
    auto* prefab = GetAssetAs<UnityEngine::GameObject>(info->asset);
    if (prefab == nullptr || !UnityEngine::Object::op_Implicit_bool(prefab)) return;
    auto* spawned = UnityEngine::Object::Instantiate(prefab);
    CleanCustomObject(spawned);
    UnityEngine::Transform* noteTransform = noteController->____noteTransform;
    spawned->get_transform()->SetParent(noteTransform, false);
    auto renderers = noteController->get_gameObject()->GetComponentsInChildren<UnityEngine::Renderer*>(true);
    for (int i = 0; i < renderers.size(); i++) {
      auto* r = renderers[i];
      if (r->get_transform()->get_parent().ptr() == noteTransform || r->get_transform().ptr() == noteTransform) {
        // When showArrow is true, preserve renderers on GameObjects named
        // "NoteArrow" or "NoteArrowGlow" so the original directional arrow
        // stays visible even though the note body has been replaced.
        if (info->showArrow) {
          auto name = r->get_gameObject()->get_name();
          if (name == "NoteArrow" || name == "NoteArrowGlow") {
            continue;
          }
        }
        r->set_enabled(false);
      }
    }
    auto* mpb = noteController->get_gameObject()->GetComponentInChildren<GlobalNamespace::MaterialPropertyBlockController*>();
    if (mpb != nullptr && UnityEngine::Object::op_Implicit_bool(mpb)) {
      auto newRenderers = spawned->GetComponentsInChildren<UnityEngine::Renderer*>(true);
      // When showArrow is true, also include the original arrow renderers in
      // the MPB so the colour block (saber colour) is applied to them correctly.
      std::vector<UnityEngine::Renderer*> combined;
      combined.reserve(newRenderers.size());
      for (int i = 0; i < newRenderers.size(); i++) {
        combined.push_back(newRenderers[i]);
      }
      if (info->showArrow) {
        auto origRenderers = noteController->get_gameObject()->GetComponentsInChildren<UnityEngine::Renderer*>(true);
        for (int i = 0; i < origRenderers.size(); i++) {
          auto* r = origRenderers[i];
          if (!r->get_enabled()) continue; // already disabled = not an arrow we kept
          auto name = r->get_gameObject()->get_name();
          if (name == "NoteArrow" || name == "NoteArrowGlow") {
            combined.push_back(r);
          }
        }
      }
      auto convertedRenderers = ArrayW<UnityW<UnityEngine::Renderer>>(combined.size());
      for (size_t i = 0; i < combined.size(); i++) {
        convertedRenderers[i] = combined[i];
      }
      mpb->____renderers = convertedRenderers;
      mpb->ApplyChanges();
    }
  }
  void ReplaceSaberVisuals(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber, UnityEngine::Transform* parent) {
    if (smc == nullptr || saber == nullptr || parent == nullptr) return;
    int type = (int)saber->get_saberType();
    auto* info = FindAssignedSaberPrefab(type);
    if (info == nullptr) return;
    auto* prefab = GetAssetAs<UnityEngine::GameObject>(info->asset);
    if (prefab == nullptr || !UnityEngine::Object::op_Implicit_bool(prefab)) return;
    auto renderers = smc->get_gameObject()->GetComponentsInChildren<UnityEngine::Renderer*>(true);
    for (int i = 0; i < renderers.size(); i++) {
      renderers[i]->set_enabled(false);
    }
    auto* spawned = UnityEngine::Object::Instantiate(prefab);
    CleanCustomObject(spawned);
    spawned->get_transform()->SetParent(parent, false);
  }
  void LateLoad() {
    auto cjdModInfo = CustomJSONData::modInfo.to_c();
    auto tracksModInfo = CModInfo{.id = "Tracks"};
    modloader_require_mod(&cjdModInfo, CMatchType::MatchType_IdOnly);
    modloader_require_mod(&tracksModInfo, CMatchType::MatchType_IdOnly);
    EnsureBehaviour();
    SongCore::API::Capabilities::RegisterCapability(kCapability);
    CustomJSONData::CustomEventCallbacks::AddCustomEventCallback(&Runtime::OnCustomEventStatic);
    SongCore::API::LevelSelect::GetLevelWasSelectedEvent() += [](SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& event) {
      Runtime::Instance().HandleLevelSelected(event);
    };
  }
  void Update() {
    if (_audioTimeSyncController != nullptr && !UnityEngine::Object::op_Implicit_bool(_audioTimeSyncController)) {
      ResetRuntime();
      return;
    }
    if (_currentBeatmapData == nullptr) {
      if (_cameraApplier && _cameraApplier->get_enabled()) {
        _cameraApplier->set_enabled(false);
      }
      return;
    }
    UpdateMaterialAnimations();
    UpdateGlobalAnimations();
    UpdateAnimatorAnimations();
    UpdateBlitEffects();
    UpdateRenderSettingAnimations();
    auto mainCam = UnityEngine::Camera::get_main();
    if (mainCam != nullptr && UnityEngine::Object::op_Implicit_bool(mainCam)) {
      auto mainCamGO = mainCam->get_gameObject();
      if (_cameraApplier == nullptr || _cameraApplier->get_gameObject() != mainCamGO) {
        if (_cameraApplier != nullptr && UnityEngine::Object::op_Implicit_bool(_cameraApplier)) {
          UnityEngine::Object::Destroy(_cameraApplier);
        }
        _cameraApplier = mainCamGO->AddComponent<CameraApplier*>();
        _cameraApplier->set_enabled(false);
      }
    }
    if (_cameraApplier) {
      bool needsBlit = !_preEffects.empty() || !_postEffects.empty();
      if (_cameraApplier->get_enabled() != needsBlit) {
        _cameraApplier->set_enabled(needsBlit);
      }
    }
  }
  void ApplyBlits(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest) {
    if (_preEffects.empty() && _postEffects.empty()) {
      UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(src), dest);
      return;
    }
    auto desc = src->get_descriptor();
    desc.set_msaaSamples(1);
    auto main = UnityEngine::RenderTexture::GetTemporary(desc);
    UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(src), main);
    auto renderEffects = [&](std::vector<ActiveBlitEffect> const& effects) {
      if (effects.empty()) return;
      for (auto const& effect : effects) {
        auto const& data = effect.data;
        UnityEngine::RenderTexture* blitSrc = nullptr;
        if (data.source == "_Main") {
          blitSrc = main;
        } else if (auto it = _declaredTextures.find(data.source); it != _declaredTextures.end()) {
          blitSrc = it->second.texture;
        } else if (auto it = _secondaryCameras.find(data.source); it != _secondaryCameras.end()) {
          blitSrc = it->second.colorRT;
        }
        if (!blitSrc) continue;
        auto sTex = static_cast<UnityEngine::Texture*>(blitSrc);
        for (auto const& targetName : data.targets) {
          if (targetName == "_Main") {
            auto temp = UnityEngine::RenderTexture::GetTemporary(desc);
            if (data.material != nullptr) UnityEngine::Graphics::Blit(sTex, temp, data.material, data.pass);
            else UnityEngine::Graphics::Blit(sTex, temp);
            UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(temp), main);
            UnityEngine::RenderTexture::ReleaseTemporary(temp);
          } else if (auto it = _declaredTextures.find(targetName); it != _declaredTextures.end()) {
            auto targetRT = it->second.texture;
            if (blitSrc == targetRT) {
              if (data.material == nullptr) continue;
              auto temp = UnityEngine::RenderTexture::GetTemporary(desc);
              UnityEngine::Graphics::Blit(sTex, temp, data.material, data.pass);
              UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(temp), targetRT);
              UnityEngine::RenderTexture::ReleaseTemporary(temp);
            } else {
              if (data.material != nullptr) UnityEngine::Graphics::Blit(sTex, targetRT, data.material, data.pass);
              else UnityEngine::Graphics::Blit(sTex, targetRT);
            }
          }
        }
      }
    };
    renderEffects(_preEffects);
    renderEffects(_postEffects);
    UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(main), dest);
    UnityEngine::RenderTexture::ReleaseTemporary(main);
  }
  void OnBehaviourDestroyed(RuntimeBehaviour* behaviour) {
    if (_behaviour == behaviour) {
      _behaviour = nullptr;
    }
  }
private:
  Runtime() = default;
  static void OnCustomEventStatic(GlobalNamespace::BeatmapCallbacksController* callbackController,
                                  CustomJSONData::CustomEventData* customEventData) {
    Runtime::Instance().HandleCustomEvent(callbackController, customEventData);
  }
  TracksAD::BeatmapAssociatedData& GetPointDataSource() {
    return _beatmapAD != nullptr ? *_beatmapAD : _fallbackBeatmapAD;
  }
  void EnsureBehaviour() {
    if (_behaviour != nullptr) {
      return;
    }
    auto* gameObject = UnityEngine::GameObject::New_ctor(u"VivifyRuntime");
    UnityEngine::Object::DontDestroyOnLoad(gameObject);
    _behaviour = gameObject->AddComponent<RuntimeBehaviour*>();
    auto mainCam = UnityEngine::Camera::get_main();
    if (mainCam != nullptr) {
      _cameraApplier = mainCam->get_gameObject()->AddComponent<CameraApplier*>();
      _cameraApplier->set_enabled(false);
    }
  }
  void HandleLevelSelected(SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& event) {
    ResetRuntime();
    _selectedLevelPath.clear();
    _selectedMapHasVivifyRequirement = false;
    if (!event.isCustom || event.customBeatmapLevel == nullptr) {
      SongCore::API::PlayButton::EnablePlayButton("Vivify");
      return;
    }
    _selectedLevelPath = std::string(event.customBeatmapLevel->customLevelPath);
    if (event.customLevelDetails) {
      auto const& requirements = event.customLevelDetails->difficultyDetails.requirements;
      _selectedMapHasVivifyRequirement = std::any_of(requirements.begin(), requirements.end(), [](std::string const& requirement) {
        return requirement == kCapability;
      });
    }
    if (_selectedMapHasVivifyRequirement) {
      MetaCore::Game::SetScoreSubmission("Vivify", false);
      std::string bundlePath = JoinPath(_selectedLevelPath, kBundleFile);
      bool bundleExists = std::filesystem::exists(bundlePath);
      if (!bundleExists) {
        std::string lowerBundlePath = JoinPath(_selectedLevelPath, "bundleandroid2021.vivify");
        if (std::filesystem::exists(lowerBundlePath)) {
            bundlePath = lowerBundlePath;
            bundleExists = true;
        }
      }
      if (!bundleExists) {
        uint32_t androidChecksum = 0;
        std::string infoPath = JoinPath(_selectedLevelPath, "Info.dat");
        if (!std::filesystem::exists(infoPath)) infoPath = JoinPath(_selectedLevelPath, "info.dat");
        if (std::filesystem::exists(infoPath)) {
          std::ifstream ifs(infoPath);
          if (!ifs.is_open()) return;
          std::string str((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
          rapidjson::Document doc;
          doc.Parse(str.c_str());
          if (!doc.HasParseError()) {
            rapidjson::Value const* customData = nullptr;
            if (doc.HasMember("_customData")) customData = &doc["_customData"];
            else if (doc.HasMember("customData")) customData = &doc["customData"];
            if (customData && customData->IsObject()) {
              rapidjson::Value const* assetBundle = nullptr;
              if (customData->HasMember("_assetBundle")) assetBundle = &(*customData)["_assetBundle"];
              else if (customData->HasMember("assetBundle")) assetBundle = &(*customData)["assetBundle"];
              if (assetBundle && assetBundle->IsObject()) {
                if (assetBundle->HasMember("_android2021") && (*assetBundle)["_android2021"].IsUint()) {
                  androidChecksum = (*assetBundle)["_android2021"].GetUint();
                } else if (assetBundle->HasMember("android2021") && (*assetBundle)["android2021"].IsUint()) {
                  androidChecksum = (*assetBundle)["android2021"].GetUint();
                }
              }
            }
          }
        }
        if (androidChecksum != 0) {
          SongCore::API::PlayButton::DisablePlayButton("Vivify", "Downloading assets...");
          DownloadBundle(androidChecksum, _selectedLevelPath, [this](bool success) {
            if (success) {
              SongCore::API::PlayButton::EnablePlayButton("Vivify");
            } else {
              SongCore::API::PlayButton::DisablePlayButton("Vivify", "Failed to download assets.");
            }
          });
        } else {
          SongCore::API::PlayButton::DisablePlayButton("Vivify", "This map does not support your game version.");
        }
      } else {
        SongCore::API::PlayButton::EnablePlayButton("Vivify");
      }
    } else {
      MetaCore::Game::SetScoreSubmission("Vivify", true);
      SongCore::API::PlayButton::EnablePlayButton("Vivify");
    }
  }
  void DownloadBundle(uint32_t checksum, std::string const& levelPath, std::function<void(bool)> callback) {
    std::string url = "https://repo.totalbs.dev/api/v1/bundles/" + std::to_string(checksum);
    std::string bundlePath = JoinPath(levelPath, kBundleFile);
    WebUtils::GetAsync<WebUtils::StringResponse>(WebUtils::URLOptions(url), [bundlePath, callback](WebUtils::StringResponse res) {
      if (res.IsSuccessful() && res.responseData.has_value()) {
        rapidjson::Document doc;
        doc.Parse(res.responseData->c_str());
        if (!doc.HasParseError() && doc.HasMember("downloadUrl") && doc["downloadUrl"].IsString()) {
          std::string downloadUrl = doc["downloadUrl"].GetString();
          WebUtils::GetAsync<WebUtils::DataResponse>(WebUtils::URLOptions(downloadUrl), [bundlePath, callback](WebUtils::DataResponse dataRes) {
            if (dataRes.IsSuccessful() && dataRes.responseData.has_value()) {
              std::ofstream os(bundlePath, std::ios::binary);
              os.write((char*)dataRes.responseData->data(), dataRes.responseData->size());
              os.close();
              BSML::MainThreadScheduler::Schedule([callback]{
                callback(true);
              });
            } else {
              BSML::MainThreadScheduler::Schedule([callback]{
                callback(false);
              });
            }
          });
        } else {
          BSML::MainThreadScheduler::Schedule([callback]{
            callback(false);
          });
        }
      } else {
        BSML::MainThreadScheduler::Schedule([callback]{
          callback(false);
        });
      }
    });
  }
  void HandleCustomEvent(GlobalNamespace::BeatmapCallbacksController* callbackController,
                         CustomJSONData::CustomEventData* customEventData) {
    if (callbackController == nullptr || customEventData == nullptr) {
      return;
    }
    std::string_view type = customEventData->type;
    if (!IsVivifyEvent(type)) {
      return;
    }
    if (!IsSupportedEvent(type)) {
      return;
    }
    auto* json = GetEventJson(customEventData);
    if (json == nullptr) {
      return;
    }
    if (!EnsureBeatmapPrepared(callbackController)) {
      return;
    }
    if (type == kInstantiatePrefabEvent) {
      InstantiatePrefab(customEventData, *json);
    } else if (type == kDestroyObjectEvent) {
      DestroyObjects(*json);
    } else if (type == kSetMaterialPropertyEvent) {
      HandleSetMaterialProperty(customEventData, *json);
    } else if (type == kSetAnimatorPropertyEvent) {
      HandleSetAnimatorProperty(customEventData, *json);
    } else if (type == kSetGlobalPropertyEvent) {
      HandleSetGlobalProperty(customEventData, *json);
    } else if (type == kBlitEvent) {
      HandleBlit(customEventData, *json);
    } else if (type == kCreateCameraEvent) {
      HandleCreateCamera(*json);
    } else if (type == kCreateScreenTextureEvent) {
      HandleCreateScreenTexture(*json);
    } else if (type == kSetCameraPropertyEvent) {
      HandleSetCameraProperty(*json);
    } else if (type == kSetRenderingSettingsEvent) {
      HandleSetRenderingSettings(customEventData, *json);
    } else if (type == kAssignObjectPrefabEvent) {
      HandleAssignObjectPrefab(customEventData, *json);
    }
  }
  void WarnUnsupportedEventOnce(std::string_view key) {
    std::string keyStr(key);
    if (_unsupportedEventWarnings.count(keyStr)) {
      return;
    }
    _unsupportedEventWarnings.emplace(keyStr);
  }
  CustomJSONData::CustomBeatmapData* GetCustomBeatmapData(GlobalNamespace::BeatmapCallbacksController* callbackController) {
    return il2cpp_utils::try_cast<CustomJSONData::CustomBeatmapData>(callbackController->_beatmapData).value_or(nullptr);
  }
  bool EnsureBeatmapPrepared(GlobalNamespace::BeatmapCallbacksController* callbackController) {
    auto* customBeatmapData = GetCustomBeatmapData(callbackController);
    if (customBeatmapData == nullptr) {
      return false;
    }
    if (_currentBeatmapData == customBeatmapData) {
      return true;
    }
    PrepareBeatmap(customBeatmapData);
    return _currentBeatmapData == customBeatmapData;
  }
  void PrepareBeatmap(CustomJSONData::CustomBeatmapData* beatmapData) {
    ResetRuntime();
    _currentBeatmapData = beatmapData;
    _beatmapAD = nullptr;
    _audioTimeSyncController = UnityEngine::Object::FindObjectOfType<GlobalNamespace::AudioTimeSyncController*>();
    if (beatmapData->customData != nullptr) {
      try {
        TracksAD::readBeatmapDataAD(beatmapData);
        _beatmapAD = &TracksAD::getBeatmapAD(beatmapData->customData);
      } catch (std::exception const& ex) {
      }
    }
    LoadMainBundle();
    PreloadInstantiatePrefabs();
  }
  void ResetRuntime() {
    _isResetting = true;
    RestoreGlobalProperties();
    std::unordered_set<UnityEngine::GameObject*> destroyed;
    for (auto& [id, prefab] : _livePrefabs) {
      if (prefab.gameObject == nullptr) {
        continue;
      }
      for (auto const& track : prefab.tracks) {
        track.UnregisterGameObject(prefab.gameObject);
      }
      if (destroyed.emplace(prefab.gameObject).second) {
        if (UnityEngine::Object::op_Implicit_bool(prefab.gameObject)) {
          UnityEngine::Object::Destroy(prefab.gameObject);
        }
      }
    }
    for (auto& [eventData, instantiate] : _instantiatePrefabs) {
      if (instantiate.instance != nullptr && destroyed.emplace(instantiate.instance).second) {
        if (UnityEngine::Object::op_Implicit_bool(instantiate.instance)) {
          UnityEngine::Object::Destroy(instantiate.instance);
        }
      }
      instantiate.instance = nullptr;
    }
    _livePrefabs.clear();
    _instantiatePrefabs.clear();
    _materialAnimations.clear();
    _globalAnimations.clear();
    _animatorAnimations.clear();
    _savedGlobalProperties.clear();
    _savedGlobalKeywords.clear();
    _assets.clear();
    for (auto& [name, dt] : _declaredTextures) {
      if (dt.texture != nullptr) {
        if (UnityEngine::Object::op_Implicit_bool(dt.texture)) {
          dt.texture->Release();
          UnityEngine::Object::Destroy(dt.texture);
        }
      }
    }
    _declaredTextures.clear();
    for (auto& [name, cam] : _secondaryCameras) {
      if (cam.colorRT != nullptr) {
        if (UnityEngine::Object::op_Implicit_bool(cam.colorRT)) {
          cam.colorRT->Release();
          UnityEngine::Object::Destroy(cam.colorRT);
        }
      }
      if (cam.depthRT != nullptr) {
        if (UnityEngine::Object::op_Implicit_bool(cam.depthRT)) {
          cam.depthRT->Release();
          UnityEngine::Object::Destroy(cam.depthRT);
        }
      }
      if (cam.camera != nullptr) {
        if (UnityEngine::Object::op_Implicit_bool(cam.camera)) {
          UnityEngine::Object::Destroy(cam.camera->get_gameObject());
        }
      }
    }
    _secondaryCameras.clear();
    _preEffects.clear();
    _postEffects.clear();
    RestoreRenderSettings();
    _renderSettingAnimations.clear();
    _savedRenderSettings.clear();
    _assignedPrefabs.clear();
    _videoPlayers.clear();
    _assetPaths.clear();
    _cameraProperties.clear();
    if (_mainBundle != nullptr) {
      _mainBundle->Unload(true);
      _mainBundle = nullptr;
    }
    _currentBeatmapData = nullptr;
    _beatmapAD = nullptr;
    _audioTimeSyncController = nullptr;
    _unsupportedEventWarnings.clear();
    _isResetting = false;
  }
  void LoadMainBundle() {
    if (_selectedLevelPath.empty()) {
      if (_selectedMapHasVivifyRequirement) {
      }
      return;
    }
    std::string bundlePath = JoinPath(_selectedLevelPath, kBundleFile);
    _mainBundle = UnityEngine::AssetBundle::LoadFromFile(StringW(bundlePath));
    if (_mainBundle == nullptr) {
      if (_selectedMapHasVivifyRequirement) {
      }
      return;
    }
    auto assetNames = _mainBundle->GetAllAssetNames();
    for (auto assetName : assetNames) {
      if (!assetName) {
        continue;
      }
      std::string key = NormalizeAssetKey(il2cpp_utils::detail::to_string(assetName));
      auto asset = _mainBundle->LoadAsset(assetName);
      if (asset != nullptr) {
        _assets[key] = asset;
      }
    }
  }
  UnityEngine::Object* GetAssetObject(std::string_view assetName) const {
    auto it = _assets.find(NormalizeAssetKey(assetName));
    return it == _assets.end() ? nullptr : it->second;
  }
  template <typename T>
  T* GetAssetAs(std::string_view assetName) const {
    auto* asset = GetAssetObject(assetName);
    if (asset == nullptr) {
      return nullptr;
    }
    return il2cpp_utils::try_cast<T>(asset).value_or(nullptr);
  }
  rapidjson::Value const* GetEventJson(CustomJSONData::CustomEventData* customEventData) const {
    if (customEventData->customData == nullptr || !customEventData->customData->value.has_value()) {
      return nullptr;
    }
    return &customEventData->customData->value.value().get();
  }
  std::optional<PointDefinitionW> MakePointDefinition(rapidjson::Value const& object,
                                                      std::string_view key,
                                                      Tracks::ffi::WrapBaseValueType type) {
    auto* value = ReadValuePtr(object, key);
    if (value == nullptr) {
      return std::nullopt;
    }
    if (!value->IsArray() && !value->IsString()) {
      return std::nullopt;
    }
    if (_beatmapAD != nullptr) {
      return _beatmapAD->getPointDefinition(object, key, type);
    }
    if (!value->IsArray()) {
      return std::nullopt;
    }
    return PointDefinitionW(*value, type, _fallbackBeatmapAD.GetBaseProviderContext());
  }
  std::vector<TrackW> ReadTracks(rapidjson::Value const& object, bool v2) {
    if (_beatmapAD == nullptr) {
      return {};
    }
    auto trackKey = v2 ? TracksAD::Constants::V2_TRACK : TracksAD::Constants::TRACK;
    auto tracks = NEJSON::ReadOptionalTracks(object, trackKey, *_beatmapAD);
    if (!tracks.has_value()) {
      return {};
    }
    return {tracks->begin(), tracks->end()};
  }
  void PreloadInstantiatePrefabs() {
    if (_currentBeatmapData == nullptr) {
      return;
    }
    bool const v2 = _currentBeatmapData->v2orEarlier;
    std::unordered_set<std::string> seenIds;
    for (auto* customEventData : _currentBeatmapData->customEventDatas) {
      if (customEventData == nullptr || customEventData->type != kInstantiatePrefabEvent) {
        continue;
      }
      auto* json = GetEventJson(customEventData);
      if (json == nullptr) {
        continue;
      }
      auto asset = ReadStringView(*json, "asset");
      if (!asset.has_value()) {
        continue;
      }
      InstantiatePrefabData data;
      data.asset = std::string(*asset);
      if (auto id = ReadStringView(*json, "id"); id.has_value()) {
        data.id = std::string(*id);
        if (!seenIds.emplace(*data.id).second) {
        }
      }
      data.transformData = Tracks::TransformData(*json, v2);
      data.tracks = ReadTracks(*json, v2);
      if (auto* prefab = GetAssetAs<UnityEngine::GameObject>(data.asset); prefab != nullptr) {
        data.instance = UnityEngine::Object::Instantiate(prefab);
        data.instance->SetActive(false);
      }
      _instantiatePrefabs.emplace(customEventData, std::move(data));
    }
  }
  std::string GetPrefabStorageId(CustomJSONData::CustomEventData* customEventData,
                                 InstantiatePrefabData const& data) const {
    if (data.id.has_value()) {
      return *data.id;
    }
    return "vivify_prefab_" + std::to_string(reinterpret_cast<std::uintptr_t>(customEventData));
  }
  void InstantiatePrefab(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const&) {
    auto it = _instantiatePrefabs.find(customEventData);
    if (it == _instantiatePrefabs.end()) {
      return;
    }
    auto& data = it->second;
    if (data.instance == nullptr) {
      auto* prefab = GetAssetAs<UnityEngine::GameObject>(data.asset);
      if (prefab == nullptr) {
        return;
      }
      data.instance = UnityEngine::Object::Instantiate(prefab);
      data.instance->SetActive(false);
    }
    std::string storageId = GetPrefabStorageId(customEventData, data);
    if (_livePrefabs.contains(storageId)) {
      DestroyPrefabById(storageId);
    }
    auto* instance = data.instance;
    instance->SetActive(true);
    auto transform = instance->get_transform();
    bool const v2 = _currentBeatmapData != nullptr && _currentBeatmapData->v2orEarlier;
    data.transformData.Apply(transform, false, v2);
    if (!data.tracks.empty()) {
      for (auto const& track : data.tracks) {
        track.RegisterGameObject(instance);
      }
      auto tracksSpan = std::span<TrackW const>(data.tracks.data(), data.tracks.size());
      Tracks::GameObjectTrackController::HandleTrackData(
          instance,
          tracksSpan,
          GlobalNamespace::StaticBeatmapObjectSpawnMovementData::kNoteLinesDistance,
          v2,
          false);
    }
    auto animatorArray = instance->GetComponentsInChildren<UnityEngine::Animator*>(true);
    std::vector<UnityEngine::Animator*> animators;
    animators.reserve(animatorArray.size());
    for (auto animator : animatorArray) {
      if (animator != nullptr) {
        animators.emplace_back(animator);
      }
    }
    _livePrefabs[storageId] = LivePrefab{
        .gameObject = instance,
        .tracks = data.tracks,
        .animators = std::move(animators),
    };
  }
  bool DestroyPrefabById(std::string const& id) {
    auto it = _livePrefabs.find(id);
    if (it == _livePrefabs.end()) {
      return false;
    }
    auto prefab = std::move(it->second);
    _livePrefabs.erase(it);
    if (prefab.gameObject != nullptr) {
      for (auto const& track : prefab.tracks) {
        track.UnregisterGameObject(prefab.gameObject);
      }
      UnityEngine::Object::Destroy(prefab.gameObject);
    }
    return true;
  }
  void DestroyObjects(rapidjson::Value const& json) {
    for (auto const& id : ReadStringListOrSingle(json, "id")) {
      if (!DestroyPrefabById(id)) {
      }
    }
  }
  float CurrentSongTime() {
    if (_audioTimeSyncController == nullptr) {
      _audioTimeSyncController = UnityEngine::Object::FindObjectOfType<GlobalNamespace::AudioTimeSyncController*>();
    }
    return _audioTimeSyncController != nullptr ? _audioTimeSyncController->get_songTime() : 0.0f;
  }
  float CurrentBpm() const {
    if (TracksStatic::bpmController) {
      return TracksStatic::bpmController->get_currentBpm();
    }
    return 0.0f;
  }
  float DurationBeatsToSeconds(float durationBeats) const {
    float bpm = CurrentBpm();
    if (bpm <= 0.0f) {
      return 0.0f;
    }
    return (60.0f * durationBeats) / bpm;
  }
  std::optional<MaterialPropertyChange> ParseMaterialProperty(rapidjson::Value const& propertyJson) {
    auto id = ReadStringView(propertyJson, "id");
    auto type = ReadStringView(propertyJson, "type");
    if (!id.has_value() || !type.has_value()) {
      return std::nullopt;
    }
    MaterialPropertyChange property;
    property.kind = ParseMaterialPropertyKind(*type);
    if (property.kind == MaterialPropertyKind::Keyword) {
      property.id = std::string(*id);
    } else {
      property.id = UnityEngine::Shader::PropertyToID(StringW(*id));
    }
    switch (property.kind) {
      case MaterialPropertyKind::Texture: {
        auto texture = ReadStringView(propertyJson, "value");
        if (!texture.has_value()) {
          return std::nullopt;
        }
        property.value = std::string(*texture);
        return property;
      }
      case MaterialPropertyKind::Color: {
        auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Vec4);
        if (!point.has_value()) {
          return std::nullopt;
        }
        property.value = *point;
        return property;
      }
      case MaterialPropertyKind::Float: {
        if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
          property.value = *point;
        } else if (auto value = ReadFloat(propertyJson, "value"); value.has_value()) {
          property.value = *value;
        } else {
          return std::nullopt;
        }
        return property;
      }
      case MaterialPropertyKind::Int: {
        if (auto value = ReadInt(propertyJson, "value"); value.has_value()) {
          property.value = *value;
          return property;
        }
        return std::nullopt;
      }
      case MaterialPropertyKind::Vector: {
        auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Vec4);
        if (!point.has_value()) {
          return std::nullopt;
        }
        property.value = *point;
        return property;
      }
      case MaterialPropertyKind::Keyword: {
        if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
          property.value = *point;
        } else if (auto value = ReadBool(propertyJson, "value"); value.has_value()) {
          property.value = *value;
        } else {
          return std::nullopt;
        }
        return property;
      }
      case MaterialPropertyKind::Unsupported:
      default:
        return std::nullopt;
    }
  }
  std::vector<MaterialPropertyChange> ParseMaterialProperties(rapidjson::Value const& json) {
    std::vector<MaterialPropertyChange> properties;
    auto* propertiesValue = ReadValuePtr(json, "properties");
    if (propertiesValue == nullptr || !propertiesValue->IsArray()) {
      return properties;
    }
    properties.reserve(propertiesValue->Size());
    for (auto const& propertyJson : propertiesValue->GetArray()) {
      if (!propertyJson.IsObject()) {
        continue;
      }
      if (auto property = ParseMaterialProperty(propertyJson); property.has_value()) {
        properties.emplace_back(std::move(*property));
      }
    }
    return properties;
  }
  std::optional<AnimatorPropertyChange> ParseAnimatorProperty(rapidjson::Value const& propertyJson) {
    auto name = ReadStringView(propertyJson, "id");
    auto type = ReadStringView(propertyJson, "type");
    if (!name.has_value() || !type.has_value()) {
      return std::nullopt;
    }
    AnimatorPropertyChange property;
    property.name = std::string(*name);
    property.kind = ParseAnimatorPropertyKind(*type);
    switch (property.kind) {
      case AnimatorPropertyKind::Bool: {
        if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
          property.value = *point;
        } else if (auto value = ReadBool(propertyJson, "value"); value.has_value()) {
          property.value = *value;
        } else {
          return std::nullopt;
        }
        return property;
      }
      case AnimatorPropertyKind::Float: {
        if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
          property.value = *point;
        } else if (auto value = ReadFloat(propertyJson, "value"); value.has_value()) {
          property.value = *value;
        } else {
          return std::nullopt;
        }
        return property;
      }
      case AnimatorPropertyKind::Integer: {
        if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
          property.value = *point;
        } else if (auto value = ReadInt(propertyJson, "value"); value.has_value()) {
          property.value = *value;
        } else {
          return std::nullopt;
        }
        return property;
      }
      case AnimatorPropertyKind::Trigger: {
        property.value = ReadBool(propertyJson, "value").value_or(true);
        return property;
      }
      case AnimatorPropertyKind::Unsupported:
      default:
        return std::nullopt;
    }
  }
  std::vector<AnimatorPropertyChange> ParseAnimatorProperties(rapidjson::Value const& json) {
    std::vector<AnimatorPropertyChange> properties;
    auto* propertiesValue = ReadValuePtr(json, "properties");
    if (propertiesValue == nullptr || !propertiesValue->IsArray()) {
      return properties;
    }
    properties.reserve(propertiesValue->Size());
    for (auto const& propertyJson : propertiesValue->GetArray()) {
      if (!propertyJson.IsObject()) {
        continue;
      }
      if (auto property = ParseAnimatorProperty(propertyJson); property.has_value()) {
        properties.emplace_back(std::move(*property));
      }
    }
    return properties;
  }
  bool IsAnimated(MaterialPropertyChange const& property) const {
    return std::holds_alternative<PointDefinitionW>(property.value);
  }
  bool IsAnimated(AnimatorPropertyChange const& property) const {
    return std::holds_alternative<PointDefinitionW>(property.value);
  }
  void ApplyMaterialProperty(UnityEngine::Material* material, MaterialPropertyChange const& property, float progress) {
    if (material == nullptr) {
      return;
    }
    switch (property.kind) {
      case MaterialPropertyKind::Texture: {
        auto propertyId = std::get<int>(property.id);
        auto* texture = GetAssetAs<UnityEngine::Texture>(std::get<std::string>(property.value));
        if (texture != nullptr) {
          material->SetTexture(propertyId, texture);
        }
        break;
      }
      case MaterialPropertyKind::Color: {
        auto propertyId = std::get<int>(property.id);
        auto const& point = std::get<PointDefinitionW>(property.value);
        material->SetColor(propertyId, VectorToColor(point.InterpolateVector4(progress)));
        break;
      }
      case MaterialPropertyKind::Float: {
        auto propertyId = std::get<int>(property.id);
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          material->SetFloat(propertyId, std::get<PointDefinitionW>(property.value).InterpolateLinear(progress));
        } else {
          material->SetFloat(propertyId, std::get<float>(property.value));
        }
        break;
      }
      case MaterialPropertyKind::Int: {
        auto propertyId = std::get<int>(property.id);
        material->SetInt(propertyId, std::get<int>(property.value));
        break;
      }
      case MaterialPropertyKind::Vector: {
        auto propertyId = std::get<int>(property.id);
        auto const& point = std::get<PointDefinitionW>(property.value);
        material->SetVector(propertyId, ToUnityVector(point.InterpolateVector4(progress)));
        break;
      }
      case MaterialPropertyKind::Keyword: {
        auto const& keyword = std::get<std::string>(property.id);
        bool enabled = false;
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          enabled = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
        } else {
          enabled = std::get<bool>(property.value);
        }
        if (enabled) {
          material->EnableKeyword(StringW(keyword));
        } else {
          material->DisableKeyword(StringW(keyword));
        }
        break;
      }
      case MaterialPropertyKind::Unsupported:
      default:
        break;
    }
  }
  void SaveOriginalGlobalProperty(MaterialPropertyChange const& property) {
    switch (property.kind) {
      case MaterialPropertyKind::Texture: {
        int propertyId = std::get<int>(property.id);
        if (!_savedGlobalProperties.contains(propertyId)) {
          _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalTexture(propertyId).ptr());
        }
        break;
      }
      case MaterialPropertyKind::Color: {
        int propertyId = std::get<int>(property.id);
        if (!_savedGlobalProperties.contains(propertyId)) {
          _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalColor(propertyId));
        }
        break;
      }
      case MaterialPropertyKind::Float: {
        int propertyId = std::get<int>(property.id);
        if (!_savedGlobalProperties.contains(propertyId)) {
          _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalFloat(propertyId));
        }
        break;
      }
      case MaterialPropertyKind::Vector: {
        int propertyId = std::get<int>(property.id);
        if (!_savedGlobalProperties.contains(propertyId)) {
          _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalVector(propertyId));
        }
        break;
      }
      case MaterialPropertyKind::Keyword: {
        auto const& keyword = std::get<std::string>(property.id);
        if (!_savedGlobalKeywords.contains(keyword)) {
          _savedGlobalKeywords.emplace(keyword, UnityEngine::Shader::IsKeywordEnabled(StringW(keyword)));
        }
        break;
      }
      case MaterialPropertyKind::Int:
      case MaterialPropertyKind::Unsupported:
      default:
        break;
    }
  }
  void ApplyGlobalProperty(MaterialPropertyChange const& property, float progress) {
    SaveOriginalGlobalProperty(property);
    switch (property.kind) {
      case MaterialPropertyKind::Texture: {
        int propertyId = std::get<int>(property.id);
        auto* texture = GetAssetAs<UnityEngine::Texture>(std::get<std::string>(property.value));
        if (texture != nullptr) {
          UnityEngine::Shader::SetGlobalTexture(propertyId, texture);
        }
        break;
      }
      case MaterialPropertyKind::Color: {
        int propertyId = std::get<int>(property.id);
        auto const& point = std::get<PointDefinitionW>(property.value);
        UnityEngine::Shader::SetGlobalColor(propertyId, VectorToColor(point.InterpolateVector4(progress)));
        break;
      }
      case MaterialPropertyKind::Float: {
        int propertyId = std::get<int>(property.id);
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          UnityEngine::Shader::SetGlobalFloat(propertyId, std::get<PointDefinitionW>(property.value).InterpolateLinear(progress));
        } else {
          UnityEngine::Shader::SetGlobalFloat(propertyId, std::get<float>(property.value));
        }
        break;
      }
      case MaterialPropertyKind::Vector: {
        int propertyId = std::get<int>(property.id);
        auto const& point = std::get<PointDefinitionW>(property.value);
        UnityEngine::Shader::SetGlobalVector(propertyId, ToUnityVector(point.InterpolateVector4(progress)));
        break;
      }
      case MaterialPropertyKind::Keyword: {
        auto const& keyword = std::get<std::string>(property.id);
        bool enabled = false;
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          enabled = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
        } else {
          enabled = std::get<bool>(property.value);
        }
        if (enabled) {
          UnityEngine::Shader::EnableKeyword(StringW(keyword));
        } else {
          UnityEngine::Shader::DisableKeyword(StringW(keyword));
        }
        break;
      }
      case MaterialPropertyKind::Int:
      case MaterialPropertyKind::Unsupported:
      default:
        break;
    }
  }
  void ApplyAnimatorProperty(std::vector<UnityEngine::Animator*> const& animators,
                             AnimatorPropertyChange const& property,
                             float progress) {
    for (auto* animator : animators) {
      if (animator == nullptr) {
        continue;
      }
      switch (property.kind) {
        case AnimatorPropertyKind::Bool: {
          bool value = false;
          if (std::holds_alternative<PointDefinitionW>(property.value)) {
            value = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
          } else {
            value = std::get<bool>(property.value);
          }
          animator->SetBool(StringW(property.name), value);
          break;
        }
        case AnimatorPropertyKind::Float: {
          float value = 0.0f;
          if (std::holds_alternative<PointDefinitionW>(property.value)) {
            value = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress);
          } else {
            value = std::get<float>(property.value);
          }
          animator->SetFloat(StringW(property.name), value);
          break;
        }
        case AnimatorPropertyKind::Integer: {
          int value = 0;
          if (std::holds_alternative<PointDefinitionW>(property.value)) {
            value = static_cast<int>(std::get<PointDefinitionW>(property.value).InterpolateLinear(progress));
          } else {
            value = std::get<int>(property.value);
          }
          animator->SetInteger(StringW(property.name), value);
          break;
        }
        case AnimatorPropertyKind::Trigger: {
          bool enabled = std::get<bool>(property.value);
          if (enabled) {
            animator->SetTrigger(StringW(property.name));
          } else {
            animator->ResetTrigger(StringW(property.name));
          }
          break;
        }
        case AnimatorPropertyKind::Unsupported:
        default:
          break;
      }
    }
  }
  void HandleSetMaterialProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
    auto asset = ReadStringView(json, "asset");
    if (!asset.has_value()) {
      return;
    }
    auto* material = GetAssetAs<UnityEngine::Material>(*asset);
    if (material == nullptr) {
      return;
    }
    auto properties = ParseMaterialProperties(json);
    if (properties.empty()) {
      return;
    }
    float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
    Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
    float startTime = customEventData->time;
    std::vector<MaterialPropertyChange> animatedProperties;
    animatedProperties.reserve(properties.size());
    float currentSongTime = CurrentSongTime();
    bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
    float initialProgress = completed ? 1.0f : 0.0f;
    for (auto const& property : properties) {
      ApplyMaterialProperty(material, property, initialProgress);
      if (!completed && IsAnimated(property)) {
        animatedProperties.emplace_back(property);
      }
    }
    if (!animatedProperties.empty()) {
      _materialAnimations.emplace_back(ActiveMaterialAnimation{
          .material = material,
          .properties = std::move(animatedProperties),
          .startTime = startTime,
          .duration = duration,
          .easing = easing,
      });
    }
  }
  void HandleSetAnimatorProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
    auto id = ReadStringView(json, "id");
    if (!id.has_value()) {
      return;
    }
    auto prefabIt = _livePrefabs.find(std::string(*id));
    if (prefabIt == _livePrefabs.end()) {
      return;
    }
    auto properties = ParseAnimatorProperties(json);
    if (properties.empty()) {
      return;
    }
    float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
    Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
    float startTime = customEventData->time;
    std::vector<AnimatorPropertyChange> animatedProperties;
    animatedProperties.reserve(properties.size());
    float currentSongTime = CurrentSongTime();
    bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
    float initialProgress = completed ? 1.0f : 0.0f;
    for (auto const& property : properties) {
      ApplyAnimatorProperty(prefabIt->second.animators, property, initialProgress);
      if (!completed && IsAnimated(property)) {
        animatedProperties.emplace_back(property);
      }
    }
    if (!animatedProperties.empty()) {
      _animatorAnimations.emplace_back(ActiveAnimatorAnimation{
          .prefabId = std::string(*id),
          .properties = std::move(animatedProperties),
          .startTime = startTime,
          .duration = duration,
          .easing = easing,
      });
    }
  }
  void HandleSetGlobalProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
    auto properties = ParseMaterialProperties(json);
    if (properties.empty()) {
      return;
    }
    float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
    Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
    float startTime = customEventData->time;
    std::vector<MaterialPropertyChange> animatedProperties;
    animatedProperties.reserve(properties.size());
    float currentSongTime = CurrentSongTime();
    bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
    float initialProgress = completed ? 1.0f : 0.0f;
    for (auto const& property : properties) {
      ApplyGlobalProperty(property, initialProgress);
      if (!completed && IsAnimated(property)) {
        animatedProperties.emplace_back(property);
      }
    }
    if (!animatedProperties.empty()) {
      _globalAnimations.emplace_back(ActiveGlobalAnimation{
          .properties = std::move(animatedProperties),
          .startTime = startTime,
          .duration = duration,
          .easing = easing,
      });
    }
  }
  float AnimationProgress(float startTime, float duration, Functions easing, float songTime) const {
    if (duration <= 0.0f) {
      return 1.0f;
    }
    float raw = std::clamp((songTime - startTime) / duration, 0.0f, 1.0f);
    return Easings::Interpolate(raw, easing);
  }
  void UpdateMaterialAnimations() {
    if (_materialAnimations.empty()) {
      return;
    }
    float songTime = CurrentSongTime();
    auto write = _materialAnimations.begin();
    for (auto read = _materialAnimations.begin(); read != _materialAnimations.end(); ++read) {
      if (read->material == nullptr) {
        continue;
      }
      float progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
      for (auto const& property : read->properties) {
        ApplyMaterialProperty(read->material, property, progress);
      }
      if (songTime < read->startTime + read->duration) {
        if (write != read) {
          *write = std::move(*read);
        }
        ++write;
      }
    }
    _materialAnimations.erase(write, _materialAnimations.end());
  }
  void UpdateGlobalAnimations() {
    if (_globalAnimations.empty()) {
      return;
    }
    float songTime = CurrentSongTime();
    auto write = _globalAnimations.begin();
    for (auto read = _globalAnimations.begin(); read != _globalAnimations.end(); ++read) {
      float progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
      for (auto const& property : read->properties) {
        ApplyGlobalProperty(property, progress);
      }
      if (songTime < read->startTime + read->duration) {
        if (write != read) {
          *write = std::move(*read);
        }
        ++write;
      }
    }
    _globalAnimations.erase(write, _globalAnimations.end());
  }
  void UpdateAnimatorAnimations() {
    if (_animatorAnimations.empty()) {
      return;
    }
    float songTime = CurrentSongTime();
    auto write = _animatorAnimations.begin();
    for (auto read = _animatorAnimations.begin(); read != _animatorAnimations.end(); ++read) {
      auto prefabIt = _livePrefabs.find(read->prefabId);
      if (prefabIt == _livePrefabs.end()) {
        continue;
      }
      float progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
      for (auto const& property : read->properties) {
        ApplyAnimatorProperty(prefabIt->second.animators, property, progress);
      }
      if (songTime < read->startTime + read->duration) {
        if (write != read) {
          *write = std::move(*read);
        }
        ++write;
      }
    }
    _animatorAnimations.erase(write, _animatorAnimations.end());
  }
#include "VivifyNewHandlers.inl"
  void RestoreGlobalProperties() {
    for (auto const& [propertyId, value] : _savedGlobalProperties) {
      if (std::holds_alternative<UnityEngine::Texture*>(value)) {
        UnityEngine::Shader::SetGlobalTexture(propertyId, std::get<UnityEngine::Texture*>(value));
      } else if (std::holds_alternative<UnityEngine::Color>(value)) {
        UnityEngine::Shader::SetGlobalColor(propertyId, std::get<UnityEngine::Color>(value));
      } else if (std::holds_alternative<float>(value)) {
        UnityEngine::Shader::SetGlobalFloat(propertyId, std::get<float>(value));
      } else if (std::holds_alternative<UnityEngine::Vector4>(value)) {
        UnityEngine::Shader::SetGlobalVector(propertyId, std::get<UnityEngine::Vector4>(value));
      }
    }
    for (auto const& [keyword, enabled] : _savedGlobalKeywords) {
      if (enabled) {
        UnityEngine::Shader::EnableKeyword(StringW(keyword));
      } else {
        UnityEngine::Shader::DisableKeyword(StringW(keyword));
      }
    }
  }
  RuntimeBehaviour* _behaviour = nullptr;
  CustomJSONData::CustomBeatmapData* _currentBeatmapData = nullptr;
  GlobalNamespace::AudioTimeSyncController* _audioTimeSyncController = nullptr;
  TracksAD::BeatmapAssociatedData* _beatmapAD = nullptr;
  TracksAD::BeatmapAssociatedData _fallbackBeatmapAD;
  UnityEngine::AssetBundle* _mainBundle = nullptr;
  std::string _selectedLevelPath;
  bool _selectedMapHasVivifyRequirement = false;
  std::unordered_map<std::string, UnityEngine::Object*> _assets;
  std::unordered_map<CustomJSONData::CustomEventData*, InstantiatePrefabData> _instantiatePrefabs;
  std::unordered_map<std::string, LivePrefab> _livePrefabs;
  std::vector<ActiveMaterialAnimation> _materialAnimations;
  std::vector<ActiveGlobalAnimation> _globalAnimations;
  std::vector<ActiveAnimatorAnimation> _animatorAnimations;
  std::unordered_map<int, SavedGlobalValue> _savedGlobalProperties;
  std::unordered_map<std::string, bool> _savedGlobalKeywords;
  std::unordered_set<std::string> _unsupportedEventWarnings;
  std::unordered_map<std::string, DeclaredTextureData> _declaredTextures;
  std::unordered_map<std::string, SecondaryCameraData> _secondaryCameras;
  std::unordered_map<std::string, CameraPropertyData> _cameraProperties;
  std::vector<ActiveBlitEffect> _preEffects;
  std::vector<ActiveBlitEffect> _postEffects;
  std::vector<ActiveRenderSettingAnimation> _renderSettingAnimations;
  std::vector<SavedRenderSetting> _savedRenderSettings;
  std::vector<AssignedPrefabInfo> _assignedPrefabs;
  CameraApplier* _cameraApplier = nullptr;
  std::vector<UnityEngine::Video::VideoPlayer*> _videoPlayers;
  std::unordered_map<std::string, std::string> _assetPaths;
  bool _isResetting = false;
};
}
MAKE_HOOK_MATCH(SaberModelController_Init, &GlobalNamespace::SaberModelController::Init, void, GlobalNamespace::SaberModelController* self, UnityEngine::Transform* parent, GlobalNamespace::Saber* saber, UnityEngine::Color trailTintColor) {
  SaberModelController_Init(self, parent, saber, trailTintColor);
  if (Runtime::Instance().GetCurrentBeatmapData() == nullptr || Runtime::Instance().IsResetting()) return;
  Runtime::Instance().ReplaceSaberVisuals(self, saber, parent);
}
MAKE_HOOK_MATCH(GameNoteController_Init, &GlobalNamespace::GameNoteController::Init, void, GlobalNamespace::GameNoteController* self, GlobalNamespace::NoteData* noteData, ByRef<GlobalNamespace::NoteSpawnData> noteSpawnData, GlobalNamespace::NoteVisualModifierType noteVisualModifierType, float cutAngleTolerance, float uniformScale) {
  GameNoteController_Init(self, noteData, noteSpawnData, noteVisualModifierType, cutAngleTolerance, uniformScale);
  if (Runtime::Instance().GetCurrentBeatmapData() == nullptr || Runtime::Instance().IsResetting()) return;
  auto* info = Runtime::Instance().FindAssignedPrefab(noteData->get_gameplayType() == GlobalNamespace::NoteData_GameplayType::Normal ? "colorNotes" : "saber", noteData);
  if (info == nullptr && noteData->get_gameplayType() == GlobalNamespace::NoteData_GameplayType::Normal) {
     info = Runtime::Instance().FindAssignedPrefab("colorNotes", noteData);
  }
  if (info) Runtime::Instance().ReplaceNoteVisuals(self, info);
}
MAKE_HOOK_MATCH(BombNoteController_Init, &GlobalNamespace::BombNoteController::Init, void, GlobalNamespace::BombNoteController* self, GlobalNamespace::NoteData* noteData, ByRef<GlobalNamespace::NoteSpawnData> noteSpawnData) {
  BombNoteController_Init(self, noteData, noteSpawnData);
  if (Runtime::Instance().GetCurrentBeatmapData() == nullptr || Runtime::Instance().IsResetting()) return;
  auto* info = Runtime::Instance().FindAssignedPrefab("bombNotes", noteData);
  if (info) Runtime::Instance().ReplaceNoteVisuals(self, info);
}
MAKE_HOOK_MATCH(BurstSliderGameNoteController_Init, &GlobalNamespace::BurstSliderGameNoteController::Init, void, GlobalNamespace::BurstSliderGameNoteController* self, GlobalNamespace::NoteData* noteData, ByRef<GlobalNamespace::NoteSpawnData> noteSpawnData, GlobalNamespace::NoteVisualModifierType noteVisualModifierType, float uniformScale) {
  BurstSliderGameNoteController_Init(self, noteData, noteSpawnData, noteVisualModifierType, uniformScale);
  if (Runtime::Instance().GetCurrentBeatmapData() == nullptr || Runtime::Instance().IsResetting()) return;
  auto* info = Runtime::Instance().FindAssignedPrefab(noteData->get_gameplayType() == GlobalNamespace::NoteData_GameplayType::BurstSliderHead ? "burstSliders" : "burstSliderElements", noteData);
  if (info) Runtime::Instance().ReplaceNoteVisuals(self, info);
}
void LateLoad() {
  Runtime::Instance().LateLoad();
  INSTALL_HOOK(PaperLogger, SaberModelController_Init);
  INSTALL_HOOK(PaperLogger, GameNoteController_Init);
  INSTALL_HOOK(PaperLogger, BombNoteController_Init);
  INSTALL_HOOK(PaperLogger, BurstSliderGameNoteController_Init);
}
void RuntimeBehaviour::Update() {
  Runtime::Instance().Update();
}
void RuntimeBehaviour::OnDestroy() {
  Runtime::Instance().OnBehaviourDestroyed(this);
}
void CameraApplier::OnRenderImage(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest) {
  Runtime::Instance().ApplyBlits(src, dest);
}
}
