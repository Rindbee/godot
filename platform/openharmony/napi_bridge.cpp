/**************************************************************************/
/*  napi_bridge.cpp                                                       */
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

#include "display_server_openharmony.h"
#include "file_access_openharmony.h"
#include "os_openharmony.h"

#include "main/main.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <arkui/native_interface.h>
#include <arkui/native_node.h>
#include <arkui/native_node_napi.h>
#include <rawfile/raw_file_manager.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "LIB_GODOT"

NAPIBridge NAPIBridge::singleton;

static napi_ref g_context_ref = nullptr;
static napi_env g_env = nullptr;
static const char *ability_utils_path = "entry/src/main/ets/utils/AbilityUtils";

ArkUI_NativeNodeAPI_1 *node_api = reinterpret_cast<ArkUI_NativeNodeAPI_1 *>(OH_ArkUI_QueryModuleInterfaceByName(ARKUI_NATIVE_NODE, "ArkUI_NativeNodeAPI_1"));

NodeData::~NodeData() {
	NAPIBridge::get_singleton()->finalize_node_data(this);
}

void NAPIBridge::_stop() {
	ERR_FAIL_COND_MSG(!singleton.started, "Stop failed, not yet started.");
	singleton.os->get_main_loop()->finalize();
	started = false;
}

napi_value NAPIBridge::_bool_to_value(napi_env__ *p_env, bool p_source) {
	napi_value result = nullptr;
	napi_get_boolean(p_env, p_source, &result);
	return result;
}

napi_value NAPIBridge::initialize(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(singleton.initialized, _bool_to_value(p_env, false), "Can only be initialized once.");

	const int ARG_COUNT = 3;
	size_t argc = ARG_COUNT;
	napi_value napi_args[ARG_COUNT] = { nullptr, nullptr };
	napi_status status = napi_get_cb_info(p_env, p_info, &argc, napi_args, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(status != napi_ok, _bool_to_value(p_env, false), vformat("napi_get_cb_info failed, error code: %d.", status));
	ERR_FAIL_COND_V_MSG(argc != ARG_COUNT, _bool_to_value(p_env, false), vformat("Incorrect number of arguments: expected %d but received %d.", ARG_COUNT, argc));

	g_env = p_env;
	if (g_context_ref != nullptr) {
		napi_delete_reference(g_env, g_context_ref);
	}
	napi_create_reference(p_env, napi_args[0], 1, &g_context_ref);

	NativeResourceManager *resource_manager = OH_ResourceManager_InitNativeResourceManager(p_env, napi_args[1]);
	ERR_FAIL_NULL_V(resource_manager, _bool_to_value(p_env, false));
	singleton.os->set_native_resource_manager(resource_manager);

	size_t count = 0;
	char buffer[PATH_MAX] = { 0 };
	napi_get_value_string_utf8(p_env, napi_args[2], buffer, PATH_MAX, &count);

	String content;
	if (count == 0) {
		FileAccessOpenHarmony::get_rawfile_content("_cl_", content);
	} else {
		content = String::utf8(buffer, count);
	}
	ERR_PRINT(vformat("content:%s,count:%d.", content, count));
	// content = "";

	Vector<String> args;
	if (!content.is_empty()) {
		Vector<String> lines = content.split("\n", false);
		for (const String &line : lines) {
			String arg = line.strip_edges();
			if (!arg.is_empty()) {
				args.push_back(arg);
			}
		}
	}

	const char **cmdline = nullptr;

	if (args.size() > 0) {
		cmdline = (const char **)memalloc(args.size() * sizeof(const char *));
		for (int i = 0; i < args.size(); i++) {
			CharString cs = args[i].utf8();
			char *flag = (char *)memalloc(cs.length() + 1);
			memcpy((void *)flag, cs.get_data(), cs.length() + 1);
			flag[cs.length()] = '\0';
			cmdline[i] = flag;
		}
	}

	Error err = Main::setup(singleton.os->get_executable_path().utf8().get_data(), args.size(), (char **)cmdline, false);

	if (cmdline) {
		for (int i = 0; i < args.size(); i++) {
			memfree((void *)cmdline[i]);
		}
		memfree(cmdline);
	}

	ERR_FAIL_COND_V_MSG(err != OK, _bool_to_value(p_env, false), vformat("Failed to setup main with error:%d.", err));

	singleton.initialized = true;
	return _bool_to_value(p_env, true);
}

napi_value NAPIBridge::finalize(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.initialized, _bool_to_value(p_env, false), "Finalize failed, not yet initialized.");
	stop(p_env, p_info);
	Main::cleanup();

	singleton.initialized = false;
	if (g_context_ref != nullptr) {
		napi_delete_reference(g_env, g_context_ref);
		g_context_ref = nullptr;
	}
	g_env = nullptr;
	return _bool_to_value(p_env, true);
}

bool NAPIBridge::start_ability(const char *p_project_id, const char *p_arguments) const {
	ERR_FAIL_NULL_V(g_env, false);
	ERR_FAIL_NULL_V(g_context_ref, false);

	napi_value module = nullptr;
	const String module_info = os->get_bundle_name() + "/entry";
	napi_load_module_with_info(g_env, ability_utils_path, module_info.utf8().get_data(), &module);
	ERR_FAIL_NULL_V(module, false);

	napi_value func = nullptr;
	napi_get_named_property(g_env, module, "startAbility", &func);
	ERR_FAIL_NULL_V(func, false);

	napi_value context = nullptr;
	napi_get_reference_value(g_env, g_context_ref, &context);
	ERR_FAIL_NULL_V(context, false);

	napi_value argv[3];
	argv[0] = context;
	napi_create_string_utf8(g_env, p_project_id, NAPI_AUTO_LENGTH, &argv[1]);
	napi_create_string_utf8(g_env, p_arguments, NAPI_AUTO_LENGTH, &argv[2]);

	napi_value result = nullptr;
	napi_status status = napi_call_function(g_env, module, func, 3, argv, &result);
	ERR_FAIL_COND_V_MSG(status != napi_ok, false, vformat("Failed to call startAbility with napi_status: %d.", status));
	bool ret = false;
	napi_get_value_bool(g_env, result, &ret);
	return ret;
}

bool NAPIBridge::terminate_self_ability() const {
	ERR_FAIL_NULL_V(g_env, false);
	ERR_FAIL_NULL_V(g_context_ref, false);

	napi_value module = nullptr;
	const String module_info = os->get_bundle_name() + "/entry";
	napi_load_module_with_info(g_env, ability_utils_path, module_info.utf8().get_data(), &module);
	ERR_FAIL_NULL_V(module, false);

	napi_value func = nullptr;
	napi_get_named_property(g_env, module, "terminateSelf", &func);
	ERR_FAIL_NULL_V(func, false);

	napi_value context = nullptr;
	napi_get_reference_value(g_env, g_context_ref, &context);
	ERR_FAIL_NULL_V(context, false);

	napi_value argv[1];
	argv[0] = context;

	napi_value result = nullptr;
	napi_status status = napi_call_function(g_env, module, func, 1, argv, &result);
	ERR_FAIL_COND_V_MSG(status != napi_ok, false, vformat("Failed to call terminateSelf with napi_status: %d.", status));
	bool ret = false;
	napi_get_value_bool(g_env, result, &ret);
	return ret;
}

napi_value NAPIBridge::set_main_window_id(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_NULL_V(p_env, nullptr);
	ERR_FAIL_NULL_V(p_info, _bool_to_value(p_env, false));

	size_t argc = 1;
	napi_value args[1] = { nullptr };
	napi_status status = napi_get_cb_info(p_env, p_info, &argc, args, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(status != napi_ok, _bool_to_value(p_env, false), vformat("napi_get_cb_info failed, error code: %d.", status));
	ERR_FAIL_COND_V_MSG(argc != 1, _bool_to_value(p_env, false), vformat("Incorrect number of arguments: expected 1 but received %d.", argc));

	int32_t window_id = -1;
	status = napi_get_value_int32(p_env, args[0], &window_id);
	ERR_FAIL_COND_V_MSG(status != napi_ok, _bool_to_value(p_env, false), vformat("napi_get_value_int32 failed, error code: %d.", status));

	DisplayServerOpenHarmony::main_native_window_id = window_id;
	return _bool_to_value(p_env, singleton.started);
}

napi_value NAPIBridge::is_started(napi_env__ *p_env, napi_callback_info p_info) {
	return _bool_to_value(p_env, singleton.started);
}

napi_value NAPIBridge::iteration(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.started, nullptr, "Iteration failed, not yet started.");
	DisplayServer::get_singleton()->process_events();
	return _bool_to_value(p_env, Main::iteration());
}

napi_value NAPIBridge::stop(napi_env__ *p_env, napi_callback_info p_info) {
	singleton._stop();
	return nullptr;
}

napi_value NAPIBridge::focus_out(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.started, nullptr, "Focus out failed, not yet started.");
	if (singleton.os->get_main_loop()) {
		singleton.os->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
	}
	return nullptr;
}
napi_value NAPIBridge::focus_in(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.started, nullptr, "Focus in failed, not yet started.");
	if (singleton.os->get_main_loop()) {
		singleton.os->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
	}
	return nullptr;
}
napi_value NAPIBridge::pause(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.started, nullptr, "Pause failed, not yet started.");
	if (singleton.os->get_main_loop()) {
		singleton.os->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
	}
	return nullptr;
}
napi_value NAPIBridge::resume(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!singleton.started, nullptr, "Resume failed, not yet started.");
	if (singleton.os->get_main_loop()) {
		singleton.os->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
	}
	return nullptr;
}

void NAPIBridge::_setup_engine(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_COND_MSG(singleton.started, "Can only be started once.");

	OHNativeWindow *native_window = OH_ArkUI_XComponent_GetNativeWindow(p_holder);
	DisplayServerOpenHarmony::main_native_window = native_window;

	Error err = Main::setup2();
	ERR_FAIL_COND_MSG(err != OK, vformat("The second stage of setup failed, err: %d.", err));

	int error = Main::start();
	ERR_FAIL_COND_MSG(error != EXIT_SUCCESS, vformat("Main start failed, err: %d.", error));

	singleton.os->get_main_loop()->initialize();
	singleton.started = true;
	ERR_PRINT(vformat("godot start successful!"));
}

void NAPIBridge::_stop_engine() {
	finalize(nullptr, nullptr);
}

void _register_node_event(ArkUI_NodeHandle p_node, ArkUI_NodeEventType p_event_type, int32_t p_target_id, void *p_user_data) {
	int32_t err = node_api->registerNodeEvent(p_node, p_event_type, p_target_id, p_user_data);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("Register node event failed, event type: %d, err: %d.", p_event_type, err));
}

void _set_attribute(ArkUI_Node *p_node, ArkUI_NodeAttributeType p_attribute, const ArkUI_AttributeItem *p_item) {
	int32_t err = node_api->setAttribute(p_node, p_attribute, p_item);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("Set attribute failed, attribute id: %d, err: %d", p_attribute, err));
}

ArkUI_Node *NAPIBridge::_create_xc(NodeData *p_node_data) {
	ERR_FAIL_NULL_V(p_node_data, nullptr);

	ArkUI_NodeHandle xc = node_api->createNode(ARKUI_NODE_XCOMPONENT);
	ERR_FAIL_NULL_V_MSG(xc, nullptr, "Create xcomponent node failed");

	// Set attributes for xc.
	ArkUI_NumberValue value[] = { { .f32 = 1.0f } };
	ArkUI_AttributeItem item = { value, 1, "node_xcomponent_id", nullptr };
	ArkUI_NumberValue value1[] = { { .u32 = ARKUI_SAFE_AREA_TYPE_SYSTEM }, { .u32 = ARKUI_SAFE_AREA_EDGE_TOP | ARKUI_SAFE_AREA_EDGE_BOTTOM } };
	ArkUI_AttributeItem vector = { value1, 2, "placeholder_node", nullptr };

	// Make the size fill the parent container.
	_set_attribute(xc, NODE_WIDTH_PERCENT, &item);
	_set_attribute(xc, NODE_HEIGHT_PERCENT, &item);
	_set_attribute(xc, NODE_EXPAND_SAFE_AREA, &vector);
	// IDs.
	_set_attribute(xc, NODE_ID, &vector);
	_set_attribute(xc, NODE_XCOMPONENT_ID, &item);
	// Xcomponent type.
	value[0].i32 = ARKUI_XCOMPONENT_TYPE_SURFACE;
	_set_attribute(xc, NODE_XCOMPONENT_TYPE, &item);
	// Focus management.
	value[0].i32 = 1;
	_set_attribute(xc, NODE_FOCUSABLE, &item);
	_set_attribute(xc, NODE_FOCUS_ON_TOUCH, &item);
	_set_attribute(xc, NODE_DEFAULT_FOCUS, &item);

	// Add callbacks for xc.

	OH_ArkUI_XComponent_RegisterOnFrameCallback(xc, DisplayServerOpenHarmony::frame_callback);

	int32_t err = node_api->addNodeEventReceiver(xc, DisplayServerOpenHarmony::node_event_receiver);
	if (err != ARKUI_ERROR_CODE_NO_ERROR) {
		ERR_PRINT(vformat("addNodeEventReceiver failed, err: %d.", err));
	}
	_register_node_event(xc, NODE_TOUCH_EVENT, p_node_data->native_window_id, p_node_data);
	// _register_node_event(xc, NODE_ON_CLICK_EVENT, p_node_data->native_window_id, p_node_data);
	_register_node_event(xc, NODE_ON_MOUSE, p_node_data->native_window_id, p_node_data);
	_register_node_event(xc, NODE_ON_AXIS, p_node_data->native_window_id, p_node_data);
	_register_node_event(xc, NODE_ON_KEY_EVENT, p_node_data->native_window_id, p_node_data);

	return xc;
}

void NAPIBridge::initialize_node_data(NodeData *p_node_data) {
	ERR_FAIL_NULL(p_node_data);

	p_node_data->node = _create_xc(p_node_data);
	if (!p_node_data->node) {
		memdelete(p_node_data);
		ERR_PRINT_ONCE("Creating xc failed.");
		return;
	}

	OH_ArkUI_SurfaceHolder *holder = OH_ArkUI_SurfaceHolder_Create(p_node_data->node);
	OH_ArkUI_SurfaceCallback *callback = OH_ArkUI_SurfaceCallback_Create();
	p_node_data->holder = holder;
	p_node_data->callback = callback;

	OH_ArkUI_SurfaceHolder_SetUserData(holder, p_node_data);

	OH_ArkUI_SurfaceCallback_SetSurfaceCreatedEvent(callback, DisplayServerOpenHarmony::surface_created_callback);
	OH_ArkUI_SurfaceCallback_SetSurfaceChangedEvent(callback, DisplayServerOpenHarmony::surface_changed_callback);
	OH_ArkUI_SurfaceCallback_SetSurfaceDestroyedEvent(callback, DisplayServerOpenHarmony::surface_destroyed_callback);
	OH_ArkUI_SurfaceCallback_SetSurfaceShowEvent(callback, DisplayServerOpenHarmony::surface_show_callback);
	OH_ArkUI_SurfaceCallback_SetSurfaceHideEvent(callback, DisplayServerOpenHarmony::surface_hide_callback);
	OH_ArkUI_SurfaceHolder_AddSurfaceCallback(holder, callback);

	int32_t error = OH_ArkUI_NodeContent_AddNode(p_node_data->parent, p_node_data->node);
	if (error != ARKUI_ERROR_CODE_NO_ERROR) {
		memdelete(p_node_data);
		ERR_PRINT(vformat("OH_ArkUI_NodeContent_AddNode failed, err: %d.", error));
		return;
	}
}

void NAPIBridge::finalize_node_data(NodeData *p_node_data) {
	ERR_FAIL_NULL(p_node_data);

	if (p_node_data->node) {
		OH_ArkUI_XComponent_UnregisterOnFrameCallback(p_node_data->node);
		node_api->removeNodeEventReceiver(p_node_data->node, DisplayServerOpenHarmony::node_event_receiver);
	}

	if (p_node_data->parent) {
		OH_ArkUI_NodeContent_RemoveNode(p_node_data->parent, p_node_data->node);
		OH_ArkUI_NodeContent_SetUserData(p_node_data->parent, nullptr);
	}

	if (p_node_data->holder) {
		if (p_node_data->callback) {
			OH_ArkUI_SurfaceHolder_RemoveSurfaceCallback(p_node_data->holder, p_node_data->callback);
		}

		OH_ArkUI_SurfaceHolder_Dispose(p_node_data->holder);
		p_node_data->holder = nullptr;
	}

	if (p_node_data->callback) {
		OH_ArkUI_SurfaceCallback_Dispose(p_node_data->callback);
		p_node_data->callback = nullptr;
	}

	if (p_node_data->node) {
		node_api->disposeNode(p_node_data->node);
		p_node_data->node = nullptr;
	}
}

void NAPIBridge::node_content_callback(ArkUI_NodeContentEvent *p_event) {
	ERR_FAIL_NULL(p_event);
	ArkUI_NodeContentHandle node = OH_ArkUI_NodeContentEvent_GetNodeContentHandle(p_event);
	ERR_FAIL_NULL(node);

	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_NodeContent_GetUserData(node));
	ERR_FAIL_NULL(nd);

	const ArkUI_NodeContentEventType type = OH_ArkUI_NodeContentEvent_GetEventType(p_event);
	if (type == NODE_CONTENT_EVENT_ON_DETACH_FROM_WINDOW) {
		ERR_FAIL_COND_MSG(!singleton.started, "Godot was not successfully started.");
		int native_window_id = nd->native_window_id;
		memdelete(nd);
		if (native_window_id == DisplayServerOpenHarmony::main_native_window_id) {
			_stop_engine();
		}
	}
}

napi_value NAPIBridge::create_native_node(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_NULL_V(node_api, nullptr);
	ERR_FAIL_NULL_V(node_api->createNode, nullptr);
	ERR_FAIL_NULL_V(node_api->addChild, nullptr);

	ERR_FAIL_NULL_V(p_env, nullptr);
	ERR_FAIL_NULL_V(p_info, nullptr);
	size_t argc = 2;
	napi_value args[2] = { nullptr, nullptr };
	napi_status status = napi_get_cb_info(p_env, p_info, &argc, args, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(status != napi_ok, nullptr, vformat("napi_get_cb_info failed, error code: %d.", status));
	ERR_FAIL_COND_V_MSG(argc != 2, nullptr, vformat("Incorrect number of arguments: expected 2 but received %d.", argc));

	int32_t native_window_id = -1;
	status = napi_get_value_int32(p_env, args[0], &native_window_id);
	ERR_FAIL_COND_V_MSG(status != napi_ok, nullptr, vformat("napi_get_value_int32 failed, error code: %d.", status));

	ArkUI_NodeContentHandle node = nullptr;
	int32_t err = OH_ArkUI_GetNodeContentFromNapiValue(p_env, args[1], &node);
	ERR_FAIL_COND_V_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, nullptr, vformat("OH_ArkUI_GetNodeContentFromNapiValue failed, error code: %d.", err));
	ERR_FAIL_NULL_V(node, nullptr);

	NodeData *nd = memnew(NodeData);
	nd->native_window_id = native_window_id;
	nd->parent = node;

	singleton.initialize_node_data(nd);
	OH_ArkUI_NodeContent_SetUserData(node, nd);
	OH_ArkUI_NodeContent_RegisterCallback(node, node_content_callback);
	return nullptr;
}

NAPIBridge::NAPIBridge() {
	singleton.os = new OS_OpenHarmony();
}

NAPIBridge::~NAPIBridge() {
	if (singleton.os) {
		delete singleton.os;
		singleton.os = nullptr;
	}
}
