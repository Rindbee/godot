/**************************************************************************/
/*  os_openharmony.h                                                      */
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

#include "audio_driver_openharmony.h"

#include "core/os/main_loop.h"
#include "drivers/unix/os_unix.h"
#include "drivers/vulkan/godot_vulkan.h"
#include "servers/audio/audio_server.h"

struct NativeResourceManager;

class OpenHarmonyLogger : public Logger {
public:
	virtual void logv(const char *p_format, va_list p_list, bool p_err) override;
};

class OS_OpenHarmony : public OS_Unix {
	NativeResourceManager *resource_manager = nullptr;

	Size2i display_size;
	MainLoop *main_loop = nullptr;
	AudioDriverOpenHarmony audio_driver;

	struct FontInfo {
		String font_name;
		HashSet<String> lang;
		HashSet<String> script;
		int weight = 400;
		int stretch = 100;
		bool italic = false;
		int priority = 0;
		String path;
	};
	mutable bool font_config_loaded = false;
	mutable HashMap<String, String> font_aliases;
	mutable List<FontInfo> fonts;
	mutable HashSet<String> font_names;

	enum DeviceType {
		DEVICE_TYPE_UNKNOWN = 0,
		DEVICE_TYPE_MOBILE = 1,
		DEVICE_TYPE_TABLET = 2,
		DEVICE_TYPE_PC = 3,
	};

	DeviceType device_type = DEVICE_TYPE_UNKNOWN;

	void _detect_device_type();

	void _load_system_font_config() const;

public:
	static const char *BUNDLE_RESOURCE_DIR;
	static const char *USER_DATA_DIR;

	static OS_OpenHarmony *get_singleton();
	OS_OpenHarmony();
	~OS_OpenHarmony();

	void set_native_resource_manager(NativeResourceManager *p_resource_manager) { resource_manager = p_resource_manager; }
	NativeResourceManager *get_native_resource_manager() const { return resource_manager; }

	virtual void initialize_core() override;
	virtual void initialize() override;

	virtual void initialize_joypads() override;

	virtual void set_main_loop(MainLoop *p_main_loop) override;
	virtual MainLoop *get_main_loop() const override;
	virtual void delete_main_loop() override;
	virtual void finalize() override;

	virtual bool request_permission(const String &p_name) override;
	virtual bool request_permissions() override;

	virtual bool _check_internal_feature_support(const String &p_feature) override;

	virtual String get_name() const override;
	virtual String get_distribution_name() const override;
	virtual String get_version() const override;
	virtual String get_model_name() const override;

	virtual String get_bundle_resource_dir() const override;
	virtual String get_cache_path() const override;
	virtual String get_config_path() const override;
	virtual String get_data_path() const override;
	virtual String get_temp_path() const override;

	virtual String get_system_dir(SystemDir p_dir, bool p_shared_storage = true) const override;

	virtual Vector<String> get_system_fonts() const override;
	virtual String get_system_font_path(const String &p_font_name, int p_weight = 400, int p_stretch = 100, bool p_italic = false) const override;
	virtual Vector<String> get_system_font_path_for_text(const String &p_font_name, const String &p_text, const String &p_locale = String(), const String &p_script = String(), int p_weight = 400, int p_stretch = 100, bool p_italic = false) const override;

	virtual String get_executable_path() const override;
	virtual Error create_instance(const List<String> &p_arguments, ProcessID *r_child_id = nullptr) override;

	virtual String get_system_ca_certificates() override;

	String get_bundle_name() const;
};
