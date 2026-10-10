/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <sstream>
#include <system_error>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

namespace rex {
namespace system {
namespace xam {

UserProfile::UserProfile() {
  // 58410A1F checks the user XUID against a mask of 0x00C0000000000000 (3<<54),
  // if non-zero, it prevents the user from playing the game.
  // "You do not have permissions to perform this operation."
  xuid_ = 0xB13EBABEBABEBABE;
  name_ = "User";

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195
  // https://github.com/arkem/py360/blob/master/py360/constants.py
  // XPROFILE_GAMER_YAXIS_INVERSION
  AddSetting(std::make_unique<Int32Setting>(0x10040002, 0));
  // XPROFILE_OPTION_CONTROLLER_VIBRATION
  AddSetting(std::make_unique<Int32Setting>(0x10040003, 3));
  // XPROFILE_GAMERCARD_ZONE
  AddSetting(std::make_unique<Int32Setting>(0x10040004, 0));
  // XPROFILE_GAMERCARD_REGION
  AddSetting(std::make_unique<Int32Setting>(0x10040005, 0));
  // XPROFILE_GAMERCARD_CRED
  AddSetting(std::make_unique<Int32Setting>(0x10040006, 0xFA));
  // XPROFILE_GAMERCARD_REP
  AddSetting(std::make_unique<FloatSetting>(0x5004000B, 0.0f));
  // XPROFILE_OPTION_VOICE_MUTED
  AddSetting(std::make_unique<Int32Setting>(0x1004000C, 0));
  // XPROFILE_OPTION_VOICE_THRU_SPEAKERS
  AddSetting(std::make_unique<Int32Setting>(0x1004000D, 0));
  // XPROFILE_OPTION_VOICE_VOLUME
  AddSetting(std::make_unique<Int32Setting>(0x1004000E, 0x64));
  // XPROFILE_GAMERCARD_MOTTO
  AddSetting(std::make_unique<UnicodeSetting>(0x402C0011, u""));
  // XPROFILE_GAMERCARD_TITLES_PLAYED
  AddSetting(std::make_unique<Int32Setting>(0x10040012, 1));
  // XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040013, 0));
  // XPROFILE_GAMER_DIFFICULTY
  AddSetting(std::make_unique<Int32Setting>(0x10040015, 0));
  // XPROFILE_GAMER_CONTROL_SENSITIVITY
  AddSetting(std::make_unique<Int32Setting>(0x10040018, 0));
  // Preferred color 1
  AddSetting(std::make_unique<Int32Setting>(0x1004001D, 0xFFFF0000u));
  // Preferred color 2
  AddSetting(std::make_unique<Int32Setting>(0x1004001E, 0xFF00FF00u));
  // XPROFILE_GAMER_ACTION_AUTO_AIM
  AddSetting(std::make_unique<Int32Setting>(0x10040022, 1));
  // XPROFILE_GAMER_ACTION_AUTO_CENTER
  AddSetting(std::make_unique<Int32Setting>(0x10040023, 0));
  // XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040024, 0));
  // XPROFILE_GAMER_RACE_TRANSMISSION
  AddSetting(std::make_unique<Int32Setting>(0x10040026, 0));
  // XPROFILE_GAMER_RACE_CAMERA_LOCATION
  AddSetting(std::make_unique<Int32Setting>(0x10040027, 0));
  // XPROFILE_GAMER_RACE_BRAKE_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040028, 0));
  // XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040029, 0));
  // XPROFILE_GAMERCARD_TITLE_CRED_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040038, 0));
  // XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040039, 0));

  // If we set this, games will try to get it.
  // XPROFILE_GAMERCARD_PICTURE_KEY
  AddSetting(std::make_unique<UnicodeSetting>(0x4064000F, u"gamercard_picture_key"));

  // XPROFILE_TITLE_SPECIFIC1
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFF));
  // XPROFILE_TITLE_SPECIFIC2
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFE));
  // XPROFILE_TITLE_SPECIFIC3
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFD));
}

void UserProfile::AddSetting(std::unique_ptr<Setting> setting, bool persistir) {
  Setting* previous_setting = setting.get();
  std::swap(settings_[setting->setting_id], previous_setting);

  // Only a setting the game wrote is saved. The constructor adds this runtime's own defaults with
  // is_set already true, and writing those would put the defaults on disk over whatever the game had
  // saved before it ever got to read them back.
  if (persistir && setting->is_set) {
    SaveSetting(setting.get());
  }

  if (previous_setting) {
    // replace: swap out the old setting from the owning list
    for (auto vec_it = setting_list_.begin(); vec_it != setting_list_.end(); ++vec_it) {
      if (vec_it->get() == previous_setting) {
        vec_it->swap(setting);
        break;
      }
    }
  } else {
    // new setting: add to the owning list
    setting_list_.push_back(std::move(setting));
  }
}

UserProfile::Setting* UserProfile::GetSetting(uint32_t setting_id) {
  const auto& it = settings_.find(setting_id);
  if (it == settings_.end()) {
    return nullptr;
  }
  UserProfile::Setting* setting = it->second;
  // Loaded on first use, whatever the setting is, for the same reason LoadSetting persists every
  // setting: the gate here was is_title_specific(), so a setting the game wrote under an id that is
  // not title-specific was created fresh by the constructor with its default and never read back.
  // Writing it and then not reading it is indistinguishable from losing it, and Call of Duty 2's
  // campaign position is one of those settings - the save file on disk is correct and the game
  // still resumes at the first mission.
  if (kernel_state_->title_id() != setting->loaded_title_id) {
    LoadSetting(setting);
  }
  return setting;
}

void UserProfile::LoadSetting(UserProfile::Setting* setting) {
  // Every setting is persisted, not only the title-specific ones.
  //
  // Xenia gates this on is_title_specific() because on a real console the non-title-specific ones live
  // in the profile container, which it does not implement - so it refuses to write them rather than
  // invent a place for them. Here there is no container either, but there is a place: the same
  // per-title directory, keyed by setting_id, which is unique across both kinds. Refusing meant that a
  // setting the game wrote came back as its default on the next run, and that is indistinguishable from
  // the game forgetting it.
  //
  // Call of Duty 2's campaign position is a saved dvar (sv_lastSaveGame, in the game's own string table)
  // and the game writes its dvars as profile settings. Dropped on the way out, the next run has no
  // "last save" to continue from and starts the campaign at its first mission.
  auto content_dir = kernel_state_->content_manager()->ResolveGameUserContentPath();
  auto setting_id = fmt::format("{:08X}", setting->setting_id);
  auto file_path = content_dir / setting_id;
  auto file = rex::filesystem::OpenFile(file_path, "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    uint32_t input_file_size = static_cast<uint32_t>(ftell(file));
    fseek(file, 0, SEEK_SET);

    std::vector<uint8_t> serialized_data(input_file_size);
    fread(serialized_data.data(), 1, serialized_data.size(), file);
    fclose(file);
    setting->Deserialize(serialized_data);
    REXSYS_INFO("[perfil] leido {:08X} tipo {}: {} byte(s) de {}", setting->setting_id,
                setting->type, serialized_data.size(), rex::path_to_utf8(file_path));
  } else {
    // Not an error: most settings are simply not there the first time. It is logged because "the game
    // read its setting and got the default" and "the game never asked" look identical in the log
    // otherwise, and that difference is the whole question.
    REXSYS_INFO("[perfil] {:08X} tipo {}: no hay fichero en {}", setting->setting_id, setting->type,
                rex::path_to_utf8(file_path));
  }
  setting->loaded_title_id = kernel_state_->title_id();
}

void UserProfile::SaveSetting(UserProfile::Setting* setting) {
  auto serialized_setting = setting->Serialize();
  auto content_dir = kernel_state_->content_manager()->ResolveGameUserContentPath();
  // The throwing overload used to kill the whole process: when the profile folder cannot be created,
  // std::filesystem_error escapes the guest call, nothing catches it, and the game dies with
  // 0xC0000409 (stack: _Throw_fs_error -> SaveSetting, see app/src/cod2_pc_excepciones.cpp).
  // A setting that cannot be written is not worth the session, so the failure is reported instead.
  std::error_code ec;
  std::filesystem::create_directories(content_dir, ec);
  if (ec) {
    REXSYS_ERROR("No se ha podido crear la carpeta de perfil «{}» para el ajuste {:08X}: {} ({})",
                 rex::path_to_utf8(content_dir), setting->setting_id, ec.message(), ec.value());
    return;
  }
  auto setting_id = fmt::format("{:08X}", setting->setting_id);
  auto file_path = content_dir / setting_id;
  auto file = rex::filesystem::OpenFile(file_path, "wb");
  if (file) {
    fwrite(serialized_setting.data(), 1, serialized_setting.size(), file);
    fclose(file);
    REXSYS_INFO("[perfil] escrito {:08X} tipo {}: {} byte(s) en {}", setting->setting_id,
                setting->type, serialized_setting.size(), rex::path_to_utf8(file_path));
  } else {
    REXSYS_ERROR("[perfil] NO se ha podido escribir {:08X} en {}", setting->setting_id,
                 rex::path_to_utf8(file_path));
  }
}

}  // namespace xam
}  // namespace system
}  // namespace rex
