/**************************************************************************/
/*  export_plugin.h                                                       */
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

#pragma once

#include "editor/export/editor_export_platform.h"

class ImageTexture;

class EditorExportPlatformOpenHarmony : public EditorExportPlatform {
	GDCLASS(EditorExportPlatformOpenHarmony, EditorExportPlatform);

	Ref<ImageTexture> logo;
	Ref<ImageTexture> run_icon;

	String _get_jdk_path() const;
	String _get_java_path() const;
	String _get_tool_path() const;
	String _get_hvigorw_path() const;
	String _get_sdk_path() const;
	String _get_hdc_path() const;
	String _get_sign_tool_path() const;

	Vector<String> devices;
	SafeFlag devices_changed;
	Mutex device_lock;
	Thread check_for_changes_thread;
	SafeFlag quit_request;
	SafeFlag has_runnable_preset;
	static void _check_for_changes_poll_thread(void *p_ud);
	void _update_preset_status();
	void _remove_dir_recursive(const String &p_dir);

	struct ABI {
		String abi;
		String arch;

		bool operator==(const ABI &p_a) const {
			return p_a.abi == abi;
		}

		ABI(const String &p_abi, const String &p_arch) {
			abi = p_abi;
			arch = p_arch;
		}
		ABI() {}
	};

	struct DefaultPermissionConfig {
		String name;
		String reason;

		DefaultPermissionConfig(const String &p_name, const String &p_reason) {
			name = p_name;
			reason = p_reason;
		}
		DefaultPermissionConfig() {}
	};

	Vector<DefaultPermissionConfig> user_grant_permission_configs;

	struct PermissionConfig {
		bool enabled = false;
		String name;
		String reason;
		String when;
	};

	String _bool_to_string(bool v) const;

	static Vector<ABI> _get_abis();
	static Vector<ABI> _get_enabled_abis(const Ref<EditorExportPreset> &p_preset);
	static String _join_abis(const Vector<ABI> &p_abis, const String &p_separator, bool p_use_arch);
	String _get_valid_basename(const Ref<EditorExportPreset> &p_preset) const;

	Error _extract_template_files(const String &p_template_path, const String &p_target_dir);

	Vector<PermissionConfig> _get_user_perms_config(const Ref<EditorExportPreset> &p_preset, String &r_permissions) const;
	Error _save_string_json_file(const Ref<EditorExportPreset> &p_preset, const String &p_string_json_file, const Vector<PermissionConfig> p_configs, const String &p_permissions);

	Error _copy_image(const Ref<EditorExportPreset> &p_preset, const String &p_image_name, const String &p_project_dir);
	Error _build_bundle(const String &p_project_dir, bool p_is_hap, bool p_debug);
	Error _sign_bundle(const Ref<EditorExportPreset> &p_preset, const String &p_path);

	Error _export_project_helper(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, bool p_should_sign, bool p_export_project_only, BitField<EditorExportPlatform::DebugFlags> p_flags);

protected:
	void _notification(int p_what);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;

	virtual void get_export_options(List<ExportOption> *r_options) const override;
	virtual bool get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const override;
	virtual String get_export_option_warning(const EditorExportPreset *p_preset, const StringName &p_name) const override;

	virtual String get_name() const override;

	virtual String get_os_name() const override;

	virtual Ref<Texture2D> get_logo() const override;

	virtual Ref<Texture2D> get_run_icon() const override;

	virtual bool poll_export() override;
	virtual int get_options_count() const override;
	virtual String get_options_tooltip() const override;
	virtual String get_option_label(int p_device) const override;
	virtual String get_option_tooltip(int p_device) const override;
	virtual String get_device_architecture(int p_device) const override;

	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override;

	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override;

	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;

	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0, bool p_notify = true) override;

	virtual Error run(const Ref<EditorExportPreset> &p_preset, int p_device, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) override;

	virtual void get_platform_features(List<String> *r_features) const override;

	EditorExportPlatformOpenHarmony();
	~EditorExportPlatformOpenHarmony();
};
