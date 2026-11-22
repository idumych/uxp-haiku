/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GfxInfo.h"
#include "mozilla/Preferences.h"

using namespace mozilla::widget;

#define EMPTY_STRING_GETTER(name)          \
  NS_IMETHODIMP GfxInfo::name(nsAString& aValue) \
  {                                        \
    aValue.Truncate();                     \
    return NS_OK;                          \
  }

#define NO_SECOND_GPU_GETTER(name)         \
  NS_IMETHODIMP GfxInfo::name(nsAString& aValue) \
  {                                        \
    return NS_ERROR_FAILURE;               \
  }

NS_IMETHODIMP
GfxInfo::GetD2DEnabled(bool* aEnabled)
{
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetDWriteEnabled(bool* aEnabled)
{
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetDWriteVersion(nsAString& aVersion)
{
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetCleartypeParameters(nsAString& aParams)
{
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDescription(nsAString& aValue)
{
  aValue.AssignLiteral("Haiku software rendering");
  return NS_OK;
}

NS_IMETHODIMP
GfxInfo::GetAdapterDriver(nsAString& aValue)
{
  aValue.AssignLiteral("app_server");
  return NS_OK;
}

EMPTY_STRING_GETTER(GetAdapterVendorID)
EMPTY_STRING_GETTER(GetAdapterDeviceID)
EMPTY_STRING_GETTER(GetAdapterSubsysID)
EMPTY_STRING_GETTER(GetAdapterRAM)
EMPTY_STRING_GETTER(GetAdapterDriverVersion)
EMPTY_STRING_GETTER(GetAdapterDriverDate)

NO_SECOND_GPU_GETTER(GetAdapterDescription2)
NO_SECOND_GPU_GETTER(GetAdapterDriver2)
NO_SECOND_GPU_GETTER(GetAdapterVendorID2)
NO_SECOND_GPU_GETTER(GetAdapterDeviceID2)
NO_SECOND_GPU_GETTER(GetAdapterSubsysID2)
NO_SECOND_GPU_GETTER(GetAdapterRAM2)
NO_SECOND_GPU_GETTER(GetAdapterDriverVersion2)
NO_SECOND_GPU_GETTER(GetAdapterDriverDate2)

NS_IMETHODIMP
GfxInfo::GetIsGPU2Active(bool* aIsGPU2Active)
{
  return NS_ERROR_FAILURE;
}

const nsTArray<GfxDriverInfo>&
GfxInfo::GetGfxDriverInfo()
{
  // No downloadable blocklist entries for Haiku.
  return *mDriverInfo;
}

nsresult
GfxInfo::GetFeatureStatusImpl(int32_t aFeature,
                              int32_t* aStatus,
                              nsAString& aSuggestedDriverVersion,
                              const nsTArray<GfxDriverInfo>& aDriverInfo,
                              nsACString& aFailureId,
                              OperatingSystem* aOS /* = nullptr */)
{
  NS_ENSURE_ARG_POINTER(aStatus);
  aSuggestedDriverVersion.SetIsVoid(true);
  if (aOS) {
    *aOS = OperatingSystem::Unknown;
  }

  switch (aFeature) {
    case nsIGfxInfo::FEATURE_WEBGL_OPENGL:
    case nsIGfxInfo::FEATURE_WEBGL_MSAA:
      // WebGL runs on OSMesa (llvmpipe) through GLContextProviderOSMesa,
      // which can be turned off with gfx.haiku.osmesa.enabled.
      if (mozilla::Preferences::GetBool("gfx.haiku.osmesa.enabled", true)) {
        *aStatus = nsIGfxInfo::FEATURE_STATUS_OK;
        return NS_OK;
      }
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_HAIKU_OSMESA_DISABLED";
      return NS_OK;
    case nsIGfxInfo::FEATURE_OPENGL_LAYERS:
    case nsIGfxInfo::FEATURE_CANVAS2D_ACCELERATION:
    case nsIGfxInfo::FEATURE_HARDWARE_VIDEO_DECODING:
    case nsIGfxInfo::FEATURE_VP8_HW_DECODE:
    case nsIGfxInfo::FEATURE_VP9_HW_DECODE:
    case nsIGfxInfo::FEATURE_WEBRTC_HW_ACCELERATION:
    case nsIGfxInfo::FEATURE_WEBRTC_HW_ACCELERATION_ENCODE:
    case nsIGfxInfo::FEATURE_WEBRTC_HW_ACCELERATION_DECODE:
      *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DEVICE;
      aFailureId = "FEATURE_FAILURE_HAIKU_NO_GL";
      return NS_OK;
    default:
      break;
  }

  return GfxInfoBase::GetFeatureStatusImpl(aFeature, aStatus,
                                           aSuggestedDriverVersion,
                                           aDriverInfo, aFailureId, aOS);
}
