// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/preference/preference_helpers.h"

#include <memory>
#include <utility>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/preference/preference_api.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/permissions/permissions_data.h"

namespace extensions {
namespace preference_helpers {

namespace {

const char kIncognitoPersistent[] = "incognito_persistent";
const char kIncognitoSessionOnly[] = "incognito_session_only";
const char kRegular[] = "regular";
const char kRegularOnly[] = "regular_only";

const char kLevelOfControlKey[] = "levelOfControl";

}  // namespace

using LevelOfControlGetter =
    base::RepeatingCallback<const char*(Profile*,
                                        const std::string& extension_id,
                                        const std::string& browser_pref,
                                        bool incognito)>;

bool StringToScope(const std::string& s,
                   ExtensionPrefsScope* scope) {
  if (s == kRegular)
    *scope = kExtensionPrefsScopeRegular;
  else if (s == kRegularOnly)
    *scope = kExtensionPrefsScopeRegularOnly;
  else if (s == kIncognitoPersistent)
    *scope = kExtensionPrefsScopeIncognitoPersistent;
  else if (s == kIncognitoSessionOnly)
    *scope = kExtensionPrefsScopeIncognitoSessionOnly;
  else
    return false;
  return true;
}

PrefService* GetProfilePrefService(Profile* profile, bool incognito) {
  if (incognito) {
    if (profile->HasPrimaryOTRProfile()) {
      return profile->GetPrimaryOTRProfile(/*create_if_needed=*/false)
          ->GetPrefs();
    }
    return profile->GetReadOnlyOffTheRecordPrefs();
  }

  return profile->GetPrefs();
}

const char* GetLevelOfControl(
    Profile* profile,
    const std::string& extension_id,
    const std::string& browser_pref,
    bool incognito) {
  PrefService* prefs = GetProfilePrefService(profile, incognito);
  bool from_incognito = false;
  bool* from_incognito_ptr = incognito ? &from_incognito : nullptr;
  const PrefService::Preference* pref = prefs->FindPreference(browser_pref);
  if (!pref->IsExtensionModifiable())
    return kNotControllable;

  if (PreferenceAPI::Get(profile)->DoesExtensionControlPref(
          extension_id,
          browser_pref,
          from_incognito_ptr)) {
    return kControlledByThisExtension;
  }

  if (PreferenceAPI::Get(profile)->CanExtensionControlPref(extension_id,
                                                           browser_pref,
                                                           incognito)) {
    return kControllableByThisExtension;
  }

  return kControlledByOtherExtensions;
}

void DispatchEventToExtensionsImpl(Profile* profile,
                                   events::HistogramValue histogram_value,
                                   const std::string& event_name,
                                   base::ListValue* args,
                                   mojom::APIPermissionID permission,
                                   bool incognito,
                                   const std::string& browser_pref,
                                   const LevelOfControlGetter level_getter) {
  EventRouter* router = EventRouter::Get(profile);
  if (!router || !router->HasEventListener(event_name))
    return;

  for (const scoped_refptr<const extensions::Extension>& extension :
       ExtensionRegistry::Get(profile)->enabled_extensions()) {
    // TODO(bauerb): Only iterate over registered event listeners.
    if (router->ExtensionHasEventListener(extension->id(), event_name) &&
        extension->permissions_data()->HasAPIPermission(permission) &&
        (!incognito || util::IsIncognitoEnabled(extension->id(), profile))) {
      // Inject level of control key-value.
      base::Value::List& args_list = args->GetList();
      DCHECK(!args_list.empty());
      DCHECK(args_list[0].is_dict());

      std::string level_of_control =
          level_getter.Run(profile, extension->id(), browser_pref, incognito);

      args_list[0].SetStringKey(kLevelOfControlKey, level_of_control);

      // If the extension is in incognito split mode,
      // a) incognito pref changes are visible only to the incognito tabs
      // b) regular pref changes are visible only to the incognito tabs if the
      //    incognito pref has not already been set
      Profile* restrict_to_profile = nullptr;
      if (IncognitoInfo::IsSplitMode(extension.get())) {
        if (incognito) {  // Handle case a).
          // If off the record profile does not exist, there should be no
          // extensions running in incognito at this time, and consequentially
          // no need to dispatch an event restricted to an incognito extension.
          // Furthermore, avoid calling GetPrimaryOTRProfile() if the profile
          // does not exist. Unnecessarily creating off the record profile is
          // undesirable, and can lead to a crash if incognito is disallowed for
          // the current profile (see https://crbug.com/796814).
          if (!profile->HasPrimaryOTRProfile())
            continue;
          restrict_to_profile =
              profile->GetPrimaryOTRProfile(/*create_if_needed=*/true);
        } else {  // Handle case b).
          bool controlled_from_incognito = false;
          bool controlled_by_extension =
              PreferenceAPI::Get(profile)->DoesExtensionControlPref(
                  extension->id(), browser_pref, &controlled_from_incognito);
          if (controlled_by_extension && controlled_from_incognito)
            restrict_to_profile = profile;
        }
      }

      base::Value::List args_copy = args->GetList().Clone();
      auto event =
          std::make_unique<Event>(histogram_value, event_name,
                                  std::move(args_copy), restrict_to_profile);
      router->DispatchEventToExtension(extension->id(), std::move(event));
    }
  }
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
void DispatchEventToExtensionsWithAshControlState(
    Profile* profile,
    events::HistogramValue histogram_value,
    const std::string& event_name,
    base::ListValue* args,
    mojom::APIPermissionID permission,
    bool incognito,
    const std::string& browser_pref,
    crosapi::mojom::PrefControlState control_state) {
  DispatchEventToExtensionsImpl(
      profile, histogram_value, event_name, args, permission, incognito,
      browser_pref,
      base::BindRepeating(&GetLevelOfControlWithAshControlState,
                          control_state));
}

const char* GetLevelOfControlWithAshControlState(
    crosapi::mojom::PrefControlState control_state,
    Profile* profile,
    const std::string& extension_id,
    const std::string& browser_pref,
    bool incognito) {
  switch (control_state) {
    case crosapi::mojom::PrefControlState::kNotExtensionControllable:
      return preference_helpers::kNotControllable;
    case crosapi::mojom::PrefControlState::kLacrosExtensionControllable:
      return preference_helpers::kControllableByThisExtension;
    case crosapi::mojom::PrefControlState::kLacrosExtensionControlled:
    case crosapi::mojom::PrefControlState::kNotExtensionControlledPrefPath:
    case crosapi::mojom::PrefControlState::kDefaultUnknown:
      return extensions::preference_helpers::GetLevelOfControl(
          profile, extension_id, browser_pref, incognito);
  }
}  // BUILDFLAG(IS_CHROMEOS_LACROS)
#endif

void DispatchEventToExtensions(Profile* profile,
                               events::HistogramValue histogram_value,
                               const std::string& event_name,
                               base::ListValue* args,
                               mojom::APIPermissionID permission,
                               bool incognito,
                               const std::string& browser_pref) {
  DispatchEventToExtensionsImpl(profile, histogram_value, event_name, args,
                                permission, incognito, browser_pref,
                                base::BindRepeating(GetLevelOfControl));
}
}  // namespace preference_helpers
}  // namespace extensions