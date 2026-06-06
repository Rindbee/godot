/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/io/zip_io.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/file_system/editor_paths.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/image_texture.h"

#include "modules/svg/image_loader_svg.h"

#define MIN_SDK_LEVEL 22

// OpenHarmony permissions, see https://developer.huawei.com/consumer/en/doc/harmonyos-guides/permissions-for-all.
static const char *OPENHARMONY_SYSTEM_GRANT_PERMISSIONS[] = {
	"ACCELEROMETER", // 7
	"ACCESS_BIOMETRIC", // 6
	"ACCESS_CAR_DISTRIBUTED_ENGINE", // 12
	"ACCESS_CERT_MANAGER", // 9
	"ACCESS_EXTENSIONAL_DEVICE_DRIVER", // 11
	"ACCESS_NOTIFICATION_POLICY", // 7
	"ACCESS_SERVICE_NAVIGATION_INFO", // 12
	"ALLOW_COREDUMP", // 23
	"BACKGROUND_MANAGER_POWER_SAVE_MODE", // 20
	"CLEAN_BACKGROUND_PROCESSES", // 7
	"COMMONEVENT_STICKY", // 7
	"CONNECT_OBJECTEDITOR_EXTENSION", // 24
	"CUSTOMIZE_MENU_ICON", // 23
	"DETECT_GESTURE", // 20
	"DISCOVER_BLUETOOTH", // 8
	"FILE_ACCESS_PERSIST", // 11 system_basic, 12
	"GET_BUNDLE_INFO", // 7
	"GET_DONOTDISTURB_STATE", // 23
	"GET_FILE_ICON", // 17
	"GET_NETWORK_INFO", // 8
	"GET_WIFI_INFO", // 8
	"GYROSCOPE", // 7
	"HDR_BRIGHTNESS", // 24
	"INHERIT_PARENT_PERMISSION", // 23
	"INPUT_KEYBOARD_CONTROLLER", // 15
	"INTERNET", // 9
	"KEEP_BACKGROUND_RUNNING", // 8
	"kernel.ALLOW_DEBUG", // 20
	"kernel.DEBUGGER", // 20
	"kernel.EXEMPT_ANONYMOUS_EXECUTABLE_MEMORY", // 23
	"kernel.IGNORE_LIBRARY_VALIDATION", // 20
	"kernel.NET_RAW", // 20
	"LOCK_WINDOW_CURSOR", // 22
	"MANAGE_INPUT_INFRARED_EMITTER", // 12 system , 16 normal
	"NDK_START_SELF_UI_ABILITY", // 15
	"NFC_CARD_EMULATION", // 8
	"NFC_TAG", // 7
	"PREPARE_APP_TERMINATE", // 10
	"PRINT", // 10
	"PRIVACY_WINDOW", // 9 system_basic, 11 normal
	"PROTECT_SCREEN_LOCK_DATA", // 12
	"PUBLISH_AGENT_REMINDER", // 7
	"READ_ACCOUNT_LOGIN_STATE", // 12
	"READ_CLOUD_SYNC_CONFIG", // 11
	"RUN_DYN_CODE", // 11
	"SET_ABILITY_INSTANCE_INFO", // 15
	"SET_NETWORK_INFO", // 8
	"SET_WIFI_INFO", // 8
	"SET_WINDOW_TRANSPARENT", // 20
	"START_WINDOW_BELOW_LOCK_SCREEN", // 21
	"STORE_PERSISTENT_DATA", // 11
	"TIMEOUT_SCREENOFF_DISABLE_LOCK", // 22
	"USE_BLUETOOTH", // 8
	"VIBRATE", // 7
	"WINDOW_TOPMOST", // 13
	nullptr
};

Vector<EditorExportPlatformOpenHarmony::ABI> EditorExportPlatformOpenHarmony::_get_abis() {
	// Should have the same order and size as get_archs.
	Vector<ABI> abis;
	// abis.push_back(ABI("armeabi-v7a", "arm32"));
	abis.push_back(ABI("arm64-v8a", "arm64"));
	// abis.push_back(ABI("x86", "x86_32"));
	abis.push_back(ABI("x86_64", "x86_64"));
	return abis;
}

Vector<EditorExportPlatformOpenHarmony::ABI> EditorExportPlatformOpenHarmony::_get_enabled_abis(const Ref<EditorExportPreset> &p_preset) {
	Vector<ABI> abis = _get_abis();
	Vector<ABI> enabled_abis;
	for (int i = 0; i < abis.size(); ++i) {
		bool is_enabled = p_preset->get("architectures/" + abis[i].abi);
		if (is_enabled) {
			enabled_abis.push_back(abis[i]);
		}
	}
	return enabled_abis;
}

String EditorExportPlatformOpenHarmony::_join_abis(const Vector<EditorExportPlatformOpenHarmony::ABI> &p_abis, const String &p_separator, bool p_use_arch) {
	String ret;
	for (int i = 0; i < p_abis.size(); ++i) {
		if (i > 0) {
			ret += p_separator;
		}
		ret += (p_use_arch) ? p_abis[i].arch : p_abis[i].abi;
	}
	return ret;
}

String EditorExportPlatformOpenHarmony::_get_valid_basename(const Ref<EditorExportPreset> &p_preset) const {
	String basename = get_project_setting(p_preset, "application/config/name");
	basename = basename.to_lower();
	return basename.validate_ascii_identifier();
}

void EditorExportPlatformOpenHarmony::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	r_features->push_back("etc2");
	r_features->push_back("astc");

	Vector<ABI> abis = _get_enabled_abis(p_preset);
	for (int i = 0; i < abis.size(); ++i) {
		r_features->push_back(abis[i].arch);
	}
}

void EditorExportPlatformOpenHarmony::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "build/export_project_only"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "build/override_export_project"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "build/compress_native_libraries"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "build/runtime_os", PROPERTY_HINT_ENUM, "HarmonyOS,OpenHarmony"), "HarmonyOS", true, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "build/min_sdk", PROPERTY_HINT_ENUM_SUGGESTION, "26.0.0,6.1.1(24),6.1.0(23),6.0.2(22)"), "6.0.2(22)", true, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "build/target_sdk", PROPERTY_HINT_ENUM_SUGGESTION, "26.0.0,6.1.1(24),6.1.0(23),6.0.2(22)"), "6.0.2(22)", true, true));

	const Vector<ABI> abis = _get_abis();
	for (int i = 0; i < abis.size(); ++i) {
		const String abi = abis[i].abi;
		const bool is_default = abi == "arm64-v8a";
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, vformat("%s/%s", PNAME("architectures"), abi)), is_default));
	}

	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "version/code", PROPERTY_HINT_RANGE, "0,4096,1,or_greater"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "version/name", PROPERTY_HINT_PLACEHOLDER_TEXT, "Leave empty to use project version"), ""));

	const String app_name = _get_valid_basename(nullptr);
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/unique_name", PROPERTY_HINT_PLACEHOLDER_TEXT, "ext.domain.name"), vformat("com.example.%s", app_name), false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "package/signed"), false, true, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/default_orientation", PROPERTY_HINT_ENUM, "unspecified,landscape,portrait,follow_recent,landscape_inverted,portrait_inverted,auto_rotation,auto_rotation_landscape,auto_rotation_portrait,auto_rotation_restricted,auto_rotation_landscape_restricted,auto_rotation_portrait_restricted,locked,auto_rotation_unspecified,follow_desktop"), "unspecified"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/image/background", PROPERTY_HINT_GLOBAL_FILE, "*.png"), "", true, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "package/image/foreground", PROPERTY_HINT_GLOBAL_FILE, "*.png"), "", true, true));

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/store_file", PROPERTY_HINT_GLOBAL_FILE, "*.p12", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/store_password", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/key_alias", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/key_password", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/sign_alg", PROPERTY_HINT_ENUM_SUGGESTION, "SHA256withECDSA", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "SHA256withECDSA", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/profile_file", PROPERTY_HINT_GLOBAL_FILE, "*.p7b", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "sign/certpath_file", PROPERTY_HINT_GLOBAL_FILE, "*.cer", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SECRET), "", false, true));

	r_options->push_back(ExportOption(PropertyInfo(Variant::PACKED_STRING_ARRAY, "permissions/extra_system_grant_permissions"), PackedStringArray()));
	const char **system_grant_perms = OPENHARMONY_SYSTEM_GRANT_PERMISSIONS;
	while (*system_grant_perms) {
		const String property_base = vformat("%s/%s", PNAME("permissions"), String(*system_grant_perms).to_lower());
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, property_base, PROPERTY_HINT_NONE), false, true));
		system_grant_perms++;
	}

	for (const DefaultPermissionConfig &E : user_grant_permission_configs) {
		const String property_base = vformat("%s/%s", PNAME("permissions"), String(E.name).to_lower());
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, property_base + "/enabled", PROPERTY_HINT_GROUP_ENABLE), false, true));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, property_base + "/reason", PROPERTY_HINT_NONE, ""), E.reason));
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, property_base + "/when", PROPERTY_HINT_ENUM, "inuse,always"), "always"));
	}
}

bool EditorExportPlatformOpenHarmony::get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const {
	if (p_preset == nullptr) {
		return true;
	}

	bool advanced_options_enabled = p_preset->are_advanced_options_enabled();

	// Hide custom template options unless advanced options are enabled
	if (p_option == "custom_template/debug" || p_option == "custom_template/release") {
		return advanced_options_enabled;
	}

	if (p_option == "build/export_project_only" ||
			p_option == "build/override_export_project" ||
			p_option == "build/compress_native_libraries") {
		return advanced_options_enabled;
	}

	// Hide architecture options unless advanced options are enabled
	if (p_option.begins_with("architectures/")) {
		return advanced_options_enabled;
	}

	if (p_option == "permissions/extra_system_grant_permissions" ||
			p_option.begins_with("permissions/kernel.")) {
		return advanced_options_enabled;
	}

	// Hide sign options unless package/signed is enabled
	bool sign_enabled = p_preset->get("package/signed");
	if (p_option.begins_with("sign/")) {
		return sign_enabled;
	}

	return true;
}

String EditorExportPlatformOpenHarmony::get_export_option_warning(const EditorExportPreset *p_preset, const StringName &p_name) const {
	if (p_preset == nullptr) {
		return String();
	}

	const String full_property_name = p_name;
	// Check sign options when package/signed is enabled
	bool sign_enabled = p_preset->get("package/signed");
	if (sign_enabled && full_property_name.begins_with("sign/")) {
		const String value = p_preset->get(p_name);
		const String property_name = full_property_name.trim_prefix("sign/").replace_char('_', ' ');
		if (value.is_empty()) {
			return vformat(TTR("Enabling signed requires %s."), property_name);
		} else if (property_name.ends_with(" file") && !FileAccess::exists(value)) {
			return vformat(TTR("The %s does not exist."), property_name);
		}
	} else if (full_property_name.begins_with("package/image/")) {
		const String image_path = p_preset->get(p_name);
		const String image_name = full_property_name.trim_prefix("package/image/");
		if (!image_path.is_empty()) {
			if (!image_path.has_extension("png")) {
				return vformat(TTR("The image for %s must be a PNG file."), image_name);
			} else if (!FileAccess::exists(image_path)) {
				return vformat(TTR("The image file for %s does not exist."), image_name);
			} else {
				Ref<Image> img = Image::load_from_file(image_path);
				if (img.is_null()) {
					return vformat(TTR("Failed to load the image for %s."), image_name);
				} else if (img->get_width() != 1024 || img->get_height() != 1024) {
					return vformat(TTR("The image for %s must be 1024x1024 pixels."), image_name);
				}
			}
		}
	}

	return String();
}

String EditorExportPlatformOpenHarmony::get_name() const {
	return "OpenHarmony";
}

String EditorExportPlatformOpenHarmony::get_os_name() const {
	return "OpenHarmony";
}

Ref<Texture2D> EditorExportPlatformOpenHarmony::get_logo() const {
	return logo;
}

Ref<Texture2D> EditorExportPlatformOpenHarmony::get_run_icon() const {
	return run_icon;
}

bool EditorExportPlatformOpenHarmony::poll_export() {
	bool dc = devices_changed.is_set();
	if (dc) {
		// don't clear unless we're reporting true, to avoid race
		devices_changed.clear();
	}
	return dc;
}

int EditorExportPlatformOpenHarmony::get_options_count() const {
	MutexLock lock(device_lock);
	return devices.size();
}

String EditorExportPlatformOpenHarmony::get_options_tooltip() const {
	return TTR("Select device from the list");
}

String EditorExportPlatformOpenHarmony::get_option_label(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, devices.size(), "");
	MutexLock lock(device_lock);
	return devices[p_index];
}

String EditorExportPlatformOpenHarmony::get_option_tooltip(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, devices.size(), "");
	MutexLock lock(device_lock);
	return "Device ID: " + devices[p_index];
}

String EditorExportPlatformOpenHarmony::get_device_architecture(int p_index) const {
	// Only arm64 is supported for now.
	return "arm64";
}

List<String> EditorExportPlatformOpenHarmony::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	return List<String>{ "hap", "app" };
}

Error EditorExportPlatformOpenHarmony::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	bool should_sign = p_preset->get("package/signed");
	bool export_project_only = p_preset->get("build/export_project_only");
	return _export_project_helper(p_preset, p_debug, p_path, should_sign, export_project_only, p_flags);
}

String EditorExportPlatformOpenHarmony::_bool_to_string(bool v) const {
	return v ? "true" : "false";
}

Error EditorExportPlatformOpenHarmony::_extract_template_files(const String &p_template_path, const String &p_target_dir) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(p_template_path.utf8().get_data(), &io);
	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Extract Template"), vformat(TTR("Could not open template for export: \"%s\"."), p_template_path));
		return ERR_FILE_CANT_READ;
	}

	Ref<DirAccess> da = DirAccess::create_for_path(p_template_path);

	int ret = unzGoToFirstFile(pkg);

	while (ret == UNZ_OK) {
		unz_file_info info;
		char filename[16384];
		ret = unzGetCurrentFileInfo(pkg, &info, filename, 16384, nullptr, 0, nullptr, 0);
		if (ret != UNZ_OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Extract Template"), vformat(TTR("Could not get file info in the template: \"%s\"."), p_template_path));
			unzClose(pkg);
			return ERR_INVALID_DATA;
		}

		const String file = String::utf8(filename);
		const String full_path = p_target_dir.path_join(file);
		if (file.ends_with("/")) {
			da->make_dir_recursive(full_path);
			ret = unzGoToNextFile(pkg);
			continue;
		}

		da->make_dir_recursive(full_path.get_base_dir());
		ret = unzOpenCurrentFile(pkg);
		if (ret != UNZ_OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Extract Template"), vformat(TTR("Could not read file: \"%s\"."), file));
			unzClose(pkg);
			return ERR_FILE_CANT_OPEN;
		}

		Ref<FileAccess> f = FileAccess::open(full_path, FileAccess::WRITE);
		if (!f.is_valid()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Extract Template"), vformat(TTR("Could not write file: \"%s\"."), full_path));
			unzCloseCurrentFile(pkg);
			unzClose(pkg);
			return ERR_FILE_CANT_WRITE;
		}

		const int buffer_size = 65536;
		uint8_t buffer[buffer_size];

		while (true) {
			int bytes_read = unzReadCurrentFile(pkg, buffer, buffer_size);
			if (bytes_read == 0) {
				break;
			}
			if (bytes_read < 0) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Extract Template"), vformat(TTR("Could not read from template: \"%s\"."), p_template_path));
				unzCloseCurrentFile(pkg);
				unzClose(pkg);
				return ERR_FILE_CORRUPT;
			}
			f->store_buffer(buffer, bytes_read);
		}
		unzCloseCurrentFile(pkg);
		ret = unzGoToNextFile(pkg);
	}
	unzClose(pkg);

	return OK;
}

Error EditorExportPlatformOpenHarmony::_copy_image(const Ref<EditorExportPreset> &p_preset, const String &p_image_name, const String &p_project_dir) {
	String image_path = p_preset->get("package/image/" + p_image_name);
	image_path = image_path.strip_edges();

	if (!image_path.is_empty() && FileAccess::exists(image_path)) {
		const String dest_bg_path = p_project_dir.path_join("entry/src/main/resources/base/media/" + p_image_name + ".png");
		Ref<DirAccess> da = DirAccess::create_for_path(p_project_dir);
		da->make_dir_recursive(dest_bg_path.get_base_dir());
		Error err = da->copy(image_path, dest_bg_path);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Configuring"), vformat(TTR("Could not copy %s image: \"%s\" to \"%s\"."), p_image_name, image_path, dest_bg_path));
			return err;
		}
	}

	return OK;
}

Error EditorExportPlatformOpenHarmony::_build_bundle(const String &p_project_dir, bool p_is_hap, bool p_debug) {
	Error err = OS::get_singleton()->set_cwd(p_project_dir);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Build"), vformat(TTR("Could not change to project directory: \"%s\"."), p_project_dir));
		return err;
	}

	const String java_bin_path = _get_jdk_path().path_join("bin");
	String system_path = OS::get_singleton()->get_environment("PATH");
#ifdef WINDOWS_ENABLED
	system_path = java_bin_path + ";" + system_path;
#else
	system_path = java_bin_path + ":" + system_path;
#endif
	OS::get_singleton()->set_environment("PATH", system_path);

	const String hvigor_path = _get_hvigorw_path();

	List<String> args;
	args.push_back(p_is_hap ? "assembleHap" : "assembleApp");
	args.push_back("-p");
	args.push_back(p_debug ? "buildMode=debug" : "buildMode=release");
	args.push_back("-p");
	args.push_back("product=default");
	if (p_is_hap) {
		args.push_back("-p");
		args.push_back("module=entry@default");
	}
	args.push_back("--mode");
	args.push_back(p_is_hap ? "module" : "project");
	args.push_back("--analyze=normal");
	args.push_back("--parallel");
	args.push_back("--incremental");
	args.push_back("--sync");
	args.push_back("--no-daemon");

	String output;
	int exit_code;
	err = OS::get_singleton()->execute(hvigor_path, args, &output, &exit_code, true, nullptr, false);
	OS::get_singleton()->set_cwd(OS::get_singleton()->get_resource_dir());
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Build"), vformat(TTR("Failed to execute build command: \"%s\".\n%s"), hvigor_path, output));
		return err;
	}

	if (exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Build"), vformat(TTR("Build failed with exit code %d:\n%s"), exit_code, output));
		return ERR_COMPILATION_FAILED;
	}

	if (is_print_verbose_enabled()) {
		print_line_rich(output);
	}

	return OK;
}

Error EditorExportPlatformOpenHarmony::_sign_bundle(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
	// For more information, see https://gitcode.com/openharmony/developtools_hapsigner.
	List<String> args;
	args.push_back("-jar");
	args.push_back(_get_sign_tool_path());
	args.push_back("sign-app");
	args.push_back("-mode");
	args.push_back("localSign");
	args.push_back("-signCode");
	args.push_back("1");
	args.push_back("-inFile");
	args.push_back(p_path);
	args.push_back("-outFile");
	args.push_back(p_path);
	args.push_back("-keyAlias");
	args.push_back(p_preset->get("sign/key_alias"));
	args.push_back("-keyPwd");
	args.push_back(p_preset->get("sign/key_password"));
	args.push_back("-signAlg");
	args.push_back(p_preset->get("sign/sign_alg"));
	args.push_back("-appCertFile");
	args.push_back(p_preset->get("sign/certpath_file"));
	args.push_back("-profileFile");
	args.push_back(p_preset->get("sign/profile_file"));
	args.push_back("-keystoreFile");
	args.push_back(p_preset->get("sign/store_file"));
	args.push_back("-keystorePwd");
	args.push_back(p_preset->get("sign/store_password"));

	String sign_output;
	int sign_exit_code;
	Error err = OS::get_singleton()->execute(_get_java_path(), args, &sign_output, &sign_exit_code, true, nullptr, false);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("Failed to sign bundle: \"%s\"."), p_path));
		return err;
	}
	if (sign_exit_code != 0) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("Sign failed with exit code %d:\n%s"), sign_exit_code, sign_output));
		return ERR_COMPILATION_FAILED;
	}

	if (is_print_verbose_enabled()) {
		print_line_rich(sign_output);
	}

	return OK;
}

Vector<EditorExportPlatformOpenHarmony::PermissionConfig> EditorExportPlatformOpenHarmony::_get_user_perms_config(const Ref<EditorExportPreset> &p_preset, String &r_permissions) const {
	StringBuilder sb;
	Vector<PermissionConfig> user_perm_configs;

	for (const DefaultPermissionConfig &E : user_grant_permission_configs) {
		const String property_base = vformat("%s/%s", PNAME("permissions"), E.name.to_lower());
		bool perm_enabled = p_preset->get(property_base + "/enabled");

		String reason = E.reason;
		String when = "always";
		if (perm_enabled) {
			reason = p_preset->get(property_base + "/reason");
			reason = reason.strip_edges();
			if (reason.is_empty()) {
				reason = E.reason;
			}
			when = p_preset->get(property_base + "/when");
			when = when.strip_edges();
			if (when.is_empty()) {
				when = "always";
			}
		}
		user_perm_configs.push_back(PermissionConfig{ perm_enabled, E.name, reason, when });
		if (!perm_enabled) {
			continue;
		}
		if (sb.num_strings_appended() > 0) {
			sb += ",";
		}
		const String perm = "ohos.permission." + E.name;
		sb += perm;
	}

	r_permissions = sb.as_string();
	return user_perm_configs;
}

Error EditorExportPlatformOpenHarmony::_save_string_json_file(const Ref<EditorExportPreset> &p_preset, const String &p_string_json_file, const Vector<PermissionConfig> p_configs, const String &p_permissions) {
	Array strings;
	String app_name = GLOBAL_GET("application/config/name");
	app_name = app_name.strip_edges();
	if (app_name.is_empty()) {
		app_name = "template";
	}
	Dictionary app_name_dict;
	app_name_dict["name"] = "app_name";
	app_name_dict["value"] = app_name;
	strings.append(app_name_dict);

	Dictionary ability_name_dict;
	ability_name_dict["name"] = "EntryAbility_label";
	ability_name_dict["value"] = app_name;
	strings.append(ability_name_dict);

	const String app_desc = GLOBAL_GET("application/config/description");
	Dictionary module_desc_dict;
	module_desc_dict["name"] = "module_desc";
	module_desc_dict["value"] = app_desc;
	strings.append(module_desc_dict);

	Dictionary entry_desc_dict;
	entry_desc_dict["name"] = "EntryAbility_desc";
	entry_desc_dict["value"] = app_desc;
	strings.append(entry_desc_dict);

	String orientation = p_preset->get("package/default_orientation");
	orientation = orientation.strip_edges();
	if (orientation.is_empty()) {
		orientation = "unspecified";
	}
	Dictionary orientation_dict;
	orientation_dict["name"] = "orientation";
	orientation_dict["value"] = orientation;
	strings.append(orientation_dict);

	Dictionary user_permissions_dict;
	user_permissions_dict["name"] = "user_permissions";
	user_permissions_dict["value"] = p_permissions;
	strings.append(user_permissions_dict);

	for (const PermissionConfig &E : p_configs) {
		Dictionary d;
		d["name"] = E.name + "_reason";
		d["value"] = E.reason;
		strings.append(d);
	}

	Dictionary data;
	data["string"] = strings;
	const String content = JSON::stringify(data, "  ", false) + "\n";

	Ref<FileAccess> fa = FileAccess::open(p_string_json_file, FileAccess::WRITE);
	if (fa.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Configuring"), vformat("Cannot open file \"%s\" for writing.", p_string_json_file));
		return ERR_FILE_CANT_WRITE;
	}
	fa->store_string(content);

	return OK;
}

Error EditorExportPlatformOpenHarmony::_export_project_helper(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, bool p_should_sign, bool p_export_project_only, BitField<EditorExportPlatform::DebugFlags> p_flags) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags);

	const String base_dir = p_path.get_base_dir();
	if (!DirAccess::exists(base_dir)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Target folder does not exist or is inaccessible: \"%s\""), base_dir));
		return ERR_FILE_BAD_PATH;
	}

	const int step_amount = p_export_project_only ? 5 : (p_should_sign ? 8 : 7);
	int step = 1;
	EditorProgress ep("export", TTR("Exporting for OpenHarmony"), step_amount, true);

	const Vector<ABI> enabled_abis = _get_enabled_abis(p_preset);
	const String export_format = p_path.get_extension().to_lower();

	StringBuilder sb;
	sb += "Exporting for OpenHarmony...\n";
	sb += "- debug build: " + _bool_to_string(p_debug) + "\n";
	sb += "- export path: " + p_path + "\n";
	sb += "- export project only: " + _bool_to_string(p_export_project_only) + "\n";
	sb += "- export bundle format: " + export_format + "\n";
	sb += "- sign build: " + _bool_to_string(p_should_sign) + "\n";
	sb += "- enabled abis: " + _join_abis(enabled_abis, ",", false) + "\n";
	sb += "- export filter: " + itos(p_preset->get_export_filter()) + "\n";
	sb += "- include filter: " + p_preset->get_include_filter() + "\n";
	sb += "- exclude filter: " + p_preset->get_exclude_filter() + "\n";
	print_verbose(sb.as_string());

	if (ep.step(TTR("Preparing templates..."), step++)) {
		return ERR_SKIP;
	}

	const String custom_template_name = p_debug ? "custom_template/debug" : "custom_template/release";
	String template_path = p_preset->get(custom_template_name);
	template_path = template_path.strip_edges();

	if (template_path.is_empty() || !FileAccess::exists(template_path)) {
		if (!template_path.is_empty()) {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Export"), vformat(TTR("Custom export template file \"%s\" not found, fallback to the default one."), template_path));
		}

		const String template_file_name = p_debug ? "openharmony_debug.zip" : "openharmony_release.zip";
		String err;
		template_path = find_export_template(template_file_name, &err);
		if (template_path.is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Export template not found.") + "\n" + err);
			return ERR_FILE_NOT_FOUND;
		}
	}

	if (ep.step(TTR("Creating export project directory..."), step++)) {
		return ERR_SKIP;
	}

	const String export_project_name = p_path.get_file().get_basename();
	const String export_project_dir = base_dir.path_join(export_project_name);

	Ref<DirAccess> da = DirAccess::create_for_path(base_dir);
	const String cwd = da->get_current_dir();

	if (da->dir_exists(export_project_dir)) {
		bool override_project = p_preset->get("build/override_export_project");
		if (!override_project) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Export project directory \"%s\" is already exists, enable \"Override Export Project\" to force override."), export_project_dir));
			return ERR_ALREADY_EXISTS;
		}

		Error err = da->change_dir(export_project_dir);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Change dir to the export project directory \"%s\" failed."), export_project_dir));
			return err;
		}
		err = da->erase_contents_recursive();
		da->change_dir(cwd);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Erase the contents in the export project directory \"%s\" failed."), export_project_dir));
			return err;
		}
	}

	Error err = da->make_dir_recursive(export_project_dir);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not create export project directory: \"%s\"."), export_project_dir));
		return err;
	}

	if (ep.step(TTR("Extracting template files..."), step++)) {
		return ERR_SKIP;
	}

	err = _extract_template_files(template_path, export_project_dir);
	if (err != OK) {
		return err;
	}

	if (ep.step(TTR("Configuring export project settings..."), step++)) {
		return ERR_SKIP;
	}

	{
		Vector<String> command_line_flags = gen_export_flags(p_flags);
		const String cl_file_path = export_project_dir.path_join("entry/src/main/resources/rawfile/_cl_");
		da->make_dir_recursive(cl_file_path.get_base_dir());

		Ref<FileAccess> cl_file = FileAccess::open(cl_file_path, FileAccess::WRITE);
		if (cl_file.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Configuring"), vformat(TTR("Could not write to command line file: \"%s\"."), cl_file_path));
			return ERR_FILE_CANT_WRITE;
		}

		for (const String &flag : command_line_flags) {
			CharString cs = (flag + "\n").utf8();
			cl_file->store_buffer((const uint8_t *)cs.get_data(), cs.length());
		}
		cl_file.unref();
	}

	const String unique_name = p_preset->get("package/unique_name");
	const int version_code = p_preset->get("version/code");
	const String version_name = p_preset->get_version("version/name", true);
	const String icon = "$media:layered_image";
	const String label = "$string:app_name";

	String user_permissions_string;
	Vector<PermissionConfig> user_perms_config = _get_user_perms_config(p_preset, user_permissions_string);

	const String string_json_path = export_project_dir.path_join("entry/src/main/resources/base/element/string.json");
	err = _save_string_json_file(p_preset, string_json_path, user_perms_config, user_permissions_string);
	if (err != OK) {
		return err;
	}

	{
		const String build_profile_path = export_project_dir.path_join("build-profile.json5");
		String content = FileAccess::get_file_as_string(build_profile_path);
		Dictionary data = JSON::parse_string(content);
		Dictionary app_profile = data.get("app", Dictionary());
		Array products = app_profile.get("products", Array());
		bool product_found = false;
		Dictionary product;
		for (int i = 0; i < products.size(); i++) {
			Dictionary d = products[i];
			if (d.get("name", "") == "default") {
				product = d;
				product_found = true;
				break;
			}
		}
		product["name"] = "default";
		product["runtimeOS"] = p_preset->get("build/runtime_os");
		product["compatibleSdkVersion"] = p_preset->get("build/min_sdk");
		product["targetSdkVersion"] = p_preset->get("build/target_sdk");
		product["bundleName"] = unique_name;
		product["versionCode"] = version_code;
		product["versionName"] = version_name;
		product["icon"] = icon;
		product["label"] = label;
		Dictionary output;
		output["artifactName"] = export_project_name;
		product["output"] = output;
		if (!product_found) {
			products.append(product);
		}
		app_profile["products"] = products;
		data["app"] = app_profile;
		content = JSON::stringify(data, "  ", false) + "\n";

		Ref<FileAccess> fa = FileAccess::open(build_profile_path, FileAccess::WRITE);
		if (fa.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Configuring"), vformat("Cannot open file \"%s\" for writing.", build_profile_path));
			return ERR_FILE_CANT_WRITE;
		}
		fa->store_string(content);
	}

	err = _copy_image(p_preset, "foreground", export_project_dir);
	if (err != OK) {
		return err;
	}

	err = _copy_image(p_preset, "background", export_project_dir);
	if (err != OK) {
		return err;
	}

	{
		const String module_json_path = export_project_dir.path_join("entry/src/main/module.json5");
		String content = FileAccess::get_file_as_string(module_json_path);
		Dictionary data = JSON::parse_string(content);
		Dictionary module = data.get("module", Dictionary());
		Array request_permissions; // Override.

		const char **perms = OPENHARMONY_SYSTEM_GRANT_PERMISSIONS;
		while (*perms) {
			String perm_name = String(*perms);
			const String property_base = vformat("%s/%s", PNAME("permissions"), perm_name.to_lower());
			bool perm_enabled = p_preset->get(property_base);
			if (perm_enabled) {
				Dictionary perm_dict;
				perm_dict["name"] = "ohos.permission." + perm_name;
				request_permissions.push_back(perm_dict);
			}
			perms++;
		}

		for (const PermissionConfig &E : user_perms_config) {
			Dictionary perm_dict;
			perm_dict["name"] = "ohos.permission." + E.name;
			perm_dict["reason"] = "$string:" + E.name + "_reason";
			Dictionary used_scene;
			Array abilities;
			abilities.push_back("EntryAbility");
			used_scene["abilities"] = abilities;
			used_scene["when"] = E.when.is_empty() ? "always" : E.when;
			perm_dict["usedScene"] = used_scene;
			request_permissions.push_back(perm_dict);
		}
		module["requestPermissions"] = request_permissions;

		module["compressNativeLibs"] = p_preset->get("build/compress_native_libraries");

		data["module"] = module;
		content = JSON::stringify(data, "  ", false) + "\n";
		Ref<FileAccess> fa = FileAccess::open(module_json_path, FileAccess::WRITE);
		if (fa.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Configuring"), vformat("Cannot open file \"%s\" for writing.", module_json_path));
			return ERR_FILE_CANT_WRITE;
		}
		fa->store_string(content);
	}

	if (ep.step(TTR("Saving project data..."), step++)) {
		return ERR_SKIP;
	}

	const String pck_path = export_project_dir.path_join("/entry/src/main/resources/rawfile/template.pck");
	err = save_pack(p_preset, p_debug, pck_path);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), TTR("Could not write package file."));
		return err;
	}

	print_line(vformat("Project exported pck successfully. %s", pck_path));

	if (p_export_project_only) {
		print_line("Project exported successfully. Build skipped as requested.");
		return OK;
	}

	if (ep.step(TTR("Building project..."), step++)) {
		return ERR_SKIP;
	}

	err = _build_bundle(export_project_dir, export_format == "hap", p_debug);
	if (err != OK) {
		return err;
	}

	if (ep.step(TTR("Copying output files..."), step++)) {
		return ERR_SKIP;
	}

	String artifact;
	if (export_format == "hap") {
		artifact = export_project_dir.path_join("entry/build/default/outputs/default/entry-default-unsigned.hap");
	} else {
		artifact = export_project_dir.path_join("build/outputs/default/" + export_project_name + "-unsigned.app");
	}

	err = da->copy(artifact, p_path);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not copy bundle file from \"%s\" to \"%s\"."), artifact, p_path));
		return err;
	}

	if (!p_should_sign) {
		return OK;
	}

	if (ep.step(TTR("Signing bundle..."), step++)) {
		return ERR_SKIP;
	}

	err = _sign_bundle(p_preset, p_path);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Code Signing"), vformat(TTR("Could not copy bundle file from \"%s\" to \"%s\"."), artifact, p_path));
		return err;
	}

	return OK;
}

void EditorExportPlatformOpenHarmony::_remove_dir_recursive(const String &p_dir) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_valid()) {
		Error err = da->erase_contents_recursive();
		ERR_FAIL_COND_MSG(err != OK, "Could not remove directory: " + p_dir);
		err = DirAccess::remove_absolute(p_dir);
		ERR_FAIL_COND_MSG(err != OK, "Could not remove directory: " + p_dir);
	}
}

Error EditorExportPlatformOpenHarmony::run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	ERR_FAIL_INDEX_V(p_device, devices.size(), ERR_INVALID_PARAMETER);

	String can_export_error;
	bool can_export_missing_templates;
	if (!can_export(p_preset, can_export_error, can_export_missing_templates)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), can_export_error);
		return ERR_UNCONFIGURED;
	}

	MutexLock lock(device_lock);

	EditorProgress ep("run", vformat(TTR("Running on %s"), devices[p_device]), 4);

	const String hdc_path = _get_hdc_path();
	if (hdc_path.is_empty()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), TTR("HDC command not found."));
		return ERR_FILE_NOT_FOUND;
	}

	// Export temporary HAP file
	if (ep.step(TTR("Exporting HAP..."), 0)) {
		return ERR_SKIP;
	}

	String tmp_export_path = EditorPaths::get_singleton()->get_temp_dir().path_join("tmpexport." + uitos(OS::get_singleton()->get_unix_time()) + ".hap");

#define CLEANUP_AND_RETURN(m_err) \
	{ \
		_remove_dir_recursive(tmp_export_path.get_basename()); \
		DirAccess::remove_file_or_error(tmp_export_path); \
		return m_err; \
	} \
	((void)0)

	// Export to temporary HAP with signing forced to true
	Error err = _export_project_helper(p_preset, true, tmp_export_path, true, false, p_debug_flags);
	if (err != OK) {
		CLEANUP_AND_RETURN(err);
	}
	print_line("HAP package path: " + tmp_export_path);

	List<String> args;
	int rv;
	String output;
	String device_id = devices[p_device];

	// Install HAP to device
	if (ep.step(TTR("Installing to device, please wait..."), 1)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	print_line("Installing to device: " + device_id);

	err = OS::get_singleton()->set_cwd(tmp_export_path.get_base_dir());
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not change to hap directory: \"%s\"."), tmp_export_path.get_base_dir()));
		return err;
	}
	args.clear();
	args.push_back("-t");
	args.push_back(device_id);
	args.push_back("install");
	args.push_back(tmp_export_path.get_file());

	output.clear();
	err = OS::get_singleton()->execute(hdc_path, args, &output, &rv, true);
	OS::get_singleton()->set_cwd(OS::get_singleton()->get_resource_dir());
	print_verbose(output);
	if (err || rv != 0 || output.contains("error")) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not install to device: %s"), output));
		CLEANUP_AND_RETURN(ERR_CANT_CREATE);
	}

	// Setup port forwarding for debugging
	if (p_debug_flags.has_flag(DEBUG_FLAG_REMOTE_DEBUG)) {
		if (ep.step(TTR("Setting up debugging..."), 2)) {
			CLEANUP_AND_RETURN(ERR_SKIP);
		}

		int dbg_port = EDITOR_GET("network/debug/remote_port");

		// remove rport with `hdc fport rm tcp:1234 tcp:1234`
		args.clear();
		args.push_back("fport");
		args.push_back("rm");
		args.push_back("tcp:" + itos(dbg_port));
		args.push_back("tcp:" + itos(dbg_port));

		output.clear();
		OS::get_singleton()->execute(hdc_path, args, &output, &rv, true);
		print_verbose(output);

		args.clear();
		args.push_back("rport");
		args.push_back("tcp:" + itos(dbg_port));
		args.push_back("tcp:" + itos(dbg_port));

		output.clear();
		OS::get_singleton()->execute(hdc_path, args, &output, &rv, true);
		print_verbose(output);
		print_line("Debug port forwarding: " + itos(dbg_port));
	}

	// Launch application
	if (ep.step(TTR("Running on device..."), 3)) {
		CLEANUP_AND_RETURN(ERR_SKIP);
	}

	args.clear();
	args.push_back("-t");
	args.push_back(device_id);
	args.push_back("shell");
	args.push_back("aa");
	args.push_back("start");
	args.push_back("-b");
	String unique_name = p_preset->get("package/unique_name");
	args.push_back(unique_name);
	args.push_back("-a");
	args.push_back("EntryAbility");

	output.clear();
	err = OS::get_singleton()->execute(hdc_path, args, &output, &rv, true);
	print_verbose(output);
	if (err || rv != 0 || output.contains("error")) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not start application on device: %s"), output));
		CLEANUP_AND_RETURN(ERR_CANT_CREATE);
	}

	print_line("Application started successfully on device: " + device_id);

	CLEANUP_AND_RETURN(OK);
#undef CLEANUP_AND_RETURN
}

void EditorExportPlatformOpenHarmony::get_platform_features(List<String> *r_features) const {
	r_features->push_back("mobile");
	r_features->push_back("openharmony");
}

bool EditorExportPlatformOpenHarmony::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
	StringBuilder err;
	bool valid = false;

	bool dvalid = false;
	bool rvalid = false;

	if (p_preset->get("custom_template/debug") != "") {
		dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
		if (!dvalid) {
			err += TTR("Custom debug template not found.");
			err += "\n";
		}
	} else {
		String err_msg;
		dvalid |= exists_export_template("openharmony_debug.zip", &err_msg);
		err += err_msg;
	}

	if (p_preset->get("custom_template/release") != "") {
		rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
		if (!rvalid) {
			err += TTR("Custom release template not found.");
			err += "\n";
		}
	} else {
		String err_msg;
		rvalid |= exists_export_template("openharmony_release.zip", &err_msg);
		err += err_msg;
	}

	valid = dvalid || rvalid;
	r_missing_templates = !valid;

	// Check architecture selection - only one should be selected
	int enabled_abis_count = _get_enabled_abis(p_preset).size();
	valid |= enabled_abis_count != 1;
	if (enabled_abis_count == 0) {
		err += TTR("At least one architecture must be selected.");
		err += "\n";
	} else if (enabled_abis_count > 1) {
		err += TTR("Only one architecture can be selected at a time.");
		err += "\n";
	}

	if (p_preset->get("build/export_project_only")) {
		if (err.get_string_length() > 0) {
			r_error = err.as_string();
		}
		return valid;
	}

	const String java_sdk_path = _get_jdk_path();
	if (java_sdk_path.is_empty()) {
		err += TTR("A valid Java SDK path is required in Editor Settings.");
		err += "\n";
		valid = false;
	} else {
		// Check for the `java` command.
		const String java_path = _get_java_path();
		if (java_path.is_empty()) {
			err += TTR("Unable to find 'java' command using the Java SDK path.") + " ";
			err += TTR("Please check the Java SDK directory specified in Editor Settings.");
			err += "\n";
			valid = false;
		}
	}

	const String tool_path = _get_tool_path();
	if (tool_path.is_empty()) {
		err += TTR("A valid Command Line Tools path is required in Editor Settings.");
		err += "\n";
		valid = false;
	} else {
		const String hvigorw_path = _get_hvigorw_path();
		if (hvigorw_path.is_empty()) {
			err += TTR("Unable to find 'hvigorw' command using the Command Line Tools path.") + " ";
			err += TTR("Please check the Command Line Tools directory specified in Editor Settings.");
			err += "\n";
			valid = false;
		}

		if (p_preset->get("package/signed")) {
			const String sign_tool_path = _get_sign_tool_path();
			if (sign_tool_path.is_empty()) {
				err += TTR("Unable to find 'hap-sign-tool.jar' using the Command Line Tools path.") + " ";
				err += TTR("Please check the Command Line Tools directory specified in Editor Settings.");
				err += "\n";
				valid = false;
			}
		}
	}

	if (err.get_string_length() > 0) {
		r_error = err.as_string();
	}

	return valid;
}

bool EditorExportPlatformOpenHarmony::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	StringBuilder err;
	bool valid = true;

	// Validate preset options using our visibility and warning methods
	List<ExportOption> options;
	get_export_options(&options);
	for (const EditorExportPlatform::ExportOption &E : options) {
		if (get_export_option_visibility(p_preset.ptr(), E.option.name)) {
			String warn = get_export_option_warning(p_preset.ptr(), E.option.name);
			err += warn;
			if (!warn.is_empty()) {
				err += "\n";
				if (E.required) {
					valid = false;
				}
			}
		}
	}

	// Check if ETC2/ASTC texture compression is enabled (required for OpenHarmony)
	if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
		valid = false;
		err += TTR("ETC2/ASTC texture compression is required for OpenHarmony export.");
		err += TTR("In Project Settings, search for 'ETC2' in the search field, or enable 'Advanced Settings' and go to Rendering > Textures > VRAM Compression to enable 'Import ETC2 ASTC'.");
		err += "\n";
	}

	// Check if Vulkan renderer is being used (required for OpenHarmony)
	String rendering_method = GLOBAL_GET("rendering/renderer/rendering_method.mobile");
	String rendering_driver = GLOBAL_GET("rendering/rendering_device/driver.openharmony");

	bool uses_vulkan = rendering_driver == "vulkan" && (rendering_method == "forward_plus" || rendering_method == "mobile");
	if (!uses_vulkan) {
		valid = false;
		err += TTR("OpenHarmony export requires Vulkan renderer.");
		err += TTR("Set rendering method to 'Forward+' or 'Mobile' and rendering driver to 'Vulkan' in Project Settings.");
		err += "\n";
	}

	if (err.get_string_length() > 0) {
		r_error = err.as_string();
	}

	return valid;
}

String EditorExportPlatformOpenHarmony::_get_jdk_path() const {
	const String java_sdk_path = EDITOR_GET("export/openharmony/java_sdk_path");
	return java_sdk_path.strip_edges();
}

String EditorExportPlatformOpenHarmony::_get_java_path() const {
	const String java_sdk_path = _get_jdk_path();
	if (java_sdk_path.is_empty()) {
		return String();
	}
#ifdef WINDOWS_ENABLED
	const String java_path = java_sdk_path.path_join("bin/java.exe");
#else
	const String java_path = java_sdk_path.path_join("bin/java");
#endif // WINDOWS_ENABLED
	if (FileAccess::exists(java_path)) {
		return java_path;
	}
	return String();
}

String EditorExportPlatformOpenHarmony::_get_tool_path() const {
	const String tool_path = EDITOR_GET("export/openharmony/openharmony_tool_path");
	return tool_path.strip_edges();
}

String EditorExportPlatformOpenHarmony::_get_hvigorw_path() const {
	const String tool_path = _get_tool_path();
	if (tool_path.is_empty()) {
		return String();
	}
#ifdef WINDOWS_ENABLED
	String hvigorw_path = tool_path.path_join("bin/hvigorw.bat");
#else
	String hvigorw_path = tool_path.path_join("bin/hvigorw");
#endif // WINDOWS_ENABLED
	if (FileAccess::exists(hvigorw_path)) {
		return hvigorw_path;
	}
#ifdef WINDOWS_ENABLED
	hvigorw_path = tool_path.path_join("tools/hvigor/bin/hvigorw.bat");
#else
	hvigorw_path = tool_path.path_join("tools/hvigor/bin/hvigorw");
#endif // WINDOWS_ENABLED
	if (FileAccess::exists(hvigorw_path)) {
		return hvigorw_path;
	}
	return String();
}

String EditorExportPlatformOpenHarmony::_get_sdk_path() const {
	const String tool_path = _get_tool_path();
	if (tool_path.is_empty()) {
		return String();
	}
	return tool_path.path_join("sdk/default/openharmony");
}

String EditorExportPlatformOpenHarmony::_get_hdc_path() const {
	const String sdk_path = _get_sdk_path();
	if (sdk_path.is_empty()) {
		return String();
	}
#ifdef WINDOWS_ENABLED
	const String hdc_path = sdk_path.path_join("toolchains/hdc.exe");
#else
	const String hdc_path = sdk_path.path_join("toolchains/hdc");
#endif // WINDOWS_ENABLED
	if (FileAccess::exists(hdc_path)) {
		return hdc_path;
	}
	return String();
}

String EditorExportPlatformOpenHarmony::_get_sign_tool_path() const {
	const String sdk_path = _get_sdk_path();
	if (sdk_path.is_empty()) {
		return "";
	}
	const String sign_tool_path = sdk_path.path_join("toolchains/lib/hap-sign-tool.jar");
	if (FileAccess::exists(sign_tool_path)) {
		return sign_tool_path;
	}
	return String();
}

EditorExportPlatformOpenHarmony::EditorExportPlatformOpenHarmony() {
	// OpenHarmony user permissions, see https://developer.huawei.com/consumer/en/doc/harmonyos-guides/permissions-for-all-user.
	user_grant_permission_configs.push_back(DefaultPermissionConfig("ACCESS_BLUETOOTH", RTR("Used for pairing and connecting to Bluetooth devices.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("ACCESS_NEARLINK", RTR("Used for pairing and connecting to NearLink devices.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("ACTIVITY_MOTION", RTR("Used for accessing motion and fitness data.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("APPROXIMATELY_LOCATION", RTR("Used for obtaining approximate location information.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("APP_TRACKING_CONSENT", RTR("Used for cross-app tracking with user consent.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("CAMERA", RTR("Used for taking photos and recording videos.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("CUSTOM_SCREEN_CAPTURE", RTR("Used for custom screen recording.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("DISTRIBUTED_DATASYNC", RTR("Used for synchronizing data across devices.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("LOCATION", RTR("Used for obtaining precise location information.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("LOCATION_IN_BACKGROUND", RTR("Used for location services when running in the background.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("MEDIA_LOCATION", RTR("Used for accessing location information in media files.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("MICROPHONE", RTR("Used for recording audio.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_AUDIO", RTR("Used for reading audio files.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_CALENDAR", RTR("Used for reading calendar events.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_HEALTH_DATA", RTR("Used for reading health and fitness data.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_IMAGEVIDEO", RTR("Used for reading images and videos from public directories.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_PASTEBOARD", RTR("Used for reading clipboard content.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_WRITE_DESKTOP_DIRECTORY", RTR("Used for reading and writing files on the desktop.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_WRITE_DOCUMENTS_DIRECTORY", RTR("Used for reading and writing files in the documents folder.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("READ_WRITE_DOWNLOAD_DIRECTORY", RTR("Used for reading and writing files in the downloads folder.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("SHORT_TERM_WRITE_IMAGEVIDEO", RTR("Used for temporarily saving images and videos.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("WRITE_AUDIO", RTR("Used for writing audio files.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("WRITE_CALENDAR", RTR("Used for writing calendar events.")));
	user_grant_permission_configs.push_back(DefaultPermissionConfig("WRITE_IMAGEVIDEO", RTR("Used for writing images and videos.")));

	if (EditorNode::get_singleton()) {
		Ref<Image> img;
		img.instantiate();
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG::create_image_from_string(img, _openharmony_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);

		ImageLoaderSVG::create_image_from_string(img, _openharmony_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);

		devices_changed.set();
		_update_preset_status();
		check_for_changes_thread.start(_check_for_changes_poll_thread, this);
	}
}

void EditorExportPlatformOpenHarmony::_check_for_changes_poll_thread(void *p_ud) {
	EditorExportPlatformOpenHarmony *eo = static_cast<EditorExportPlatformOpenHarmony *>(p_ud);

	while (!eo->quit_request.is_set()) {
		const String hdc_path = eo->_get_hdc_path();
		if (eo->has_runnable_preset.is_set() && hdc_path.is_empty() && EditorNode::get_singleton()->is_editor_ready()) {
			String devices_output;
			List<String> args{ "list", "targets" };
			int ec;
			OS::get_singleton()->execute(hdc_path, args, &devices_output, &ec);

			Vector<String> ds = devices_output.split("\n");
			Vector<String> ldevices;

			for (int i = 0; i < ds.size(); i++) {
				String d = ds[i].strip_edges();
				if (d.is_empty() || d == "[Empty]") {
					continue;
				}
				ldevices.push_back(d);
			}

			MutexLock lock(eo->device_lock);

			bool different = false;

			if (eo->devices.size() != ldevices.size()) {
				different = true;
			} else {
				for (int i = 0; i < eo->devices.size(); i++) {
					if (eo->devices[i] != ldevices[i]) {
						different = true;
						break;
					}
				}
			}

			if (different) {
				eo->devices = ldevices;
				eo->devices_changed.set();
			}
		}

		uint64_t sleep = 200;
		uint64_t wait = 3000000;
		uint64_t time = OS::get_singleton()->get_ticks_usec();
		while (OS::get_singleton()->get_ticks_usec() - time < wait) {
			OS::get_singleton()->delay_usec(1000 * sleep);
			if (eo->quit_request.is_set()) {
				break;
			}
		}
	}
}

void EditorExportPlatformOpenHarmony::_update_preset_status() {
	const int preset_count = EditorExport::get_singleton()->get_export_preset_count();
	bool has_runnable = false;

	for (int i = 0; i < preset_count; i++) {
		const Ref<EditorExportPreset> &preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset->get_platform() == this && preset->is_runnable()) {
			has_runnable = true;
			break;
		}
	}

	if (has_runnable) {
		has_runnable_preset.set();
	} else {
		has_runnable_preset.clear();
	}
	devices_changed.set();
}

void EditorExportPlatformOpenHarmony::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_POSTINITIALIZE: {
			if (EditorExport::get_singleton()) {
				EditorExport::get_singleton()->connect_presets_runnable_updated(callable_mp(this, &EditorExportPlatformOpenHarmony::_update_preset_status));
			}
		} break;
	}
}

EditorExportPlatformOpenHarmony::~EditorExportPlatformOpenHarmony() {
	quit_request.set();
	if (check_for_changes_thread.is_started()) {
		check_for_changes_thread.wait_to_finish();
	}
}
