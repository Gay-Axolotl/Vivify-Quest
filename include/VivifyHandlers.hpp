#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <variant>
#include <vector>
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/DepthTextureMode.hpp"
#include "UnityEngine/CameraClearFlags.hpp"
#include "UnityEngine/FogMode.hpp"
#include "UnityEngine/Rendering/AmbientMode.hpp"
namespace Vivify {
namespace Parsing {
inline std::optional<UnityEngine::RenderTextureFormat> ParseRenderTextureFormat(std::string_view s) {
  if (s == "ARGB32") return UnityEngine::RenderTextureFormat::ARGB32;
  if (s == "Depth") return UnityEngine::RenderTextureFormat::Depth;
  if (s == "ARGBHalf") return UnityEngine::RenderTextureFormat::ARGBHalf;
  if (s == "Shadowmap") return UnityEngine::RenderTextureFormat::Shadowmap;
  if (s == "RGB565") return UnityEngine::RenderTextureFormat::RGB565;
  if (s == "ARGB4444") return UnityEngine::RenderTextureFormat::ARGB4444;
  if (s == "ARGB1555") return UnityEngine::RenderTextureFormat::ARGB1555;
  if (s == "Default") return UnityEngine::RenderTextureFormat::Default;
  if (s == "ARGB2101010") return UnityEngine::RenderTextureFormat::ARGB2101010;
  if (s == "DefaultHDR") return UnityEngine::RenderTextureFormat::DefaultHDR;
  if (s == "ARGBFloat") return UnityEngine::RenderTextureFormat::ARGBFloat;
  if (s == "RGFloat") return UnityEngine::RenderTextureFormat::RGFloat;
  if (s == "RGHalf") return UnityEngine::RenderTextureFormat::RGHalf;
  if (s == "RFloat") return UnityEngine::RenderTextureFormat::RFloat;
  if (s == "RHalf") return UnityEngine::RenderTextureFormat::RHalf;
  if (s == "R8") return UnityEngine::RenderTextureFormat::R8;
  if (s == "ARGBInt") return UnityEngine::RenderTextureFormat::ARGBInt;
  if (s == "RGInt") return UnityEngine::RenderTextureFormat::RGInt;
  if (s == "RInt") return UnityEngine::RenderTextureFormat::RInt;
  if (s == "RGB111110Float") return UnityEngine::RenderTextureFormat::RGB111110Float;
  if (s == "RG32") return UnityEngine::RenderTextureFormat::RG32;
  return std::nullopt;
}
inline std::optional<UnityEngine::FilterMode> ParseFilterMode(std::string_view s) {
  if (s == "Point") return UnityEngine::FilterMode::Point;
  if (s == "Bilinear") return UnityEngine::FilterMode::Bilinear;
  if (s == "Trilinear") return UnityEngine::FilterMode::Trilinear;
  return std::nullopt;
}
inline std::optional<UnityEngine::CameraClearFlags> ParseClearFlags(std::string_view s) {
  if (s == "Skybox") return UnityEngine::CameraClearFlags::Skybox;
  if (s == "Color" || s == "SolidColor") return UnityEngine::CameraClearFlags::Color;
  if (s == "Depth") return UnityEngine::CameraClearFlags::Depth;
  if (s == "Nothing") return UnityEngine::CameraClearFlags::Nothing;
  return std::nullopt;
}
inline std::optional<UnityEngine::DepthTextureMode> ParseDepthTextureMode(std::string_view s) {
  if (s == "None") return UnityEngine::DepthTextureMode::None;
  if (s == "Depth") return UnityEngine::DepthTextureMode::Depth;
  if (s == "DepthNormals") return UnityEngine::DepthTextureMode::DepthNormals;
  if (s == "MotionVectors") return UnityEngine::DepthTextureMode::MotionVectors;
  return std::nullopt;
}
inline std::optional<UnityEngine::FogMode> ParseFogMode(std::string_view s) {
  if (s == "Linear") return UnityEngine::FogMode::Linear;
  if (s == "Exponential") return UnityEngine::FogMode::Exponential;
  if (s == "ExponentialSquared") return UnityEngine::FogMode::ExponentialSquared;
  return std::nullopt;
}
}
}
