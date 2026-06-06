/**************************************************************************/
/*  libgodot_harmony.cpp                                                  */
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

#include "napi_bridge.h"
#include "os_openharmony.h"

#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "main/main.h"

#include <hilog/log.h>
#include <napi/native_api.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "LIB_GODOT"

static OS_OpenHarmony *os = nullptr;

static GodotInstance *instance = nullptr;

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created.");

	os = new OS_OpenHarmony();

	Error err = Main::setup(p_argv[0], p_argc - 1, &p_argv[1], false);
	if (err != OK) {
		return nullptr;
	}

	instance = memnew(GodotInstance);
	if (!instance->initialize(p_init_func)) {
		memdelete(instance);
		// Note: When Godot Engine supports reinitialization, clear the instance pointer here.
		instance = nullptr;
		return nullptr;
	}

	return (GDExtensionObjectPtr)instance;
}

void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		instance = nullptr;
		Main::cleanup();
		if (os) {
			delete os;
			os = nullptr;
		}
	}
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
	OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, LOG_TAG, "Init libgodot begins");
	if ((env == nullptr) || (exports == nullptr)) {
		OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "Failed to init libgodot as env or exports is null");
		return nullptr;
	}
	napi_property_descriptor desc[] = {
		{ "initialize", nullptr, NAPIBridge::initialize, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "finalize", nullptr, NAPIBridge::finalize, nullptr, nullptr, nullptr, napi_default, nullptr },

		{ "setMainWindowId", nullptr, NAPIBridge::set_main_window_id, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "is_started", nullptr, NAPIBridge::is_started, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "iteration", nullptr, NAPIBridge::iteration, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "stop", nullptr, NAPIBridge::stop, nullptr, nullptr, nullptr, napi_default, nullptr },

		{ "focus_out", nullptr, NAPIBridge::focus_out, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "focus_in", nullptr, NAPIBridge::focus_in, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "pause", nullptr, NAPIBridge::pause, nullptr, nullptr, nullptr, napi_default, nullptr },
		{ "resume", nullptr, NAPIBridge::resume, nullptr, nullptr, nullptr, napi_default, nullptr },

		{ "createNativeNode", nullptr, NAPIBridge::create_native_node, nullptr, nullptr, nullptr, napi_default, nullptr },
	};
	if (napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc) != napi_ok) {
		OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_TAG, "Failed to init libgodot as napi_define_properties failed");
		return nullptr;
	}
	return exports;
}
EXTERN_C_END

static napi_module godotModule = {
	.nm_version = 1,
	.nm_flags = 0,
	.nm_filename = nullptr,
	.nm_register_func = Init,
	.nm_modname = "godot",
	.nm_priv = ((void *)0),
	.reserved = { 0 },
};

EXTERN_C_START
__attribute__((visibility("default"))) __attribute__((constructor)) void RegisterModule(void) {
	napi_module_register(&godotModule);
}
EXTERN_C_END
