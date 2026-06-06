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
static OS_OpenHarmony *os = nullptr;

static bool initialized = false;
static bool started = false;

ArkUI_NativeNodeAPI_1 *node_api = reinterpret_cast<ArkUI_NativeNodeAPI_1 *>(OH_ArkUI_QueryModuleInterfaceByName(ARKUI_NATIVE_NODE, "ArkUI_NativeNodeAPI_1"));

napi_value NAPIBridge::_bool_to_value(napi_env__ *p_env, bool p_source) {
	napi_value result = nullptr;
	napi_get_boolean(p_env, p_source, &result);
	return result;
}

napi_value NAPIBridge::initialize(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(initialized, _bool_to_value(p_env, false), "Can only be initialized once.");

	size_t argc = 1;
	napi_value napi_args[1] = { nullptr };
	napi_status status = napi_get_cb_info(p_env, p_info, &argc, napi_args, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(status != napi_ok, nullptr, vformat("napi_get_cb_info failed, error code: %d.", status));
	ERR_FAIL_COND_V_MSG(argc != 1, nullptr, vformat("Incorrect number of arguments: expected 1 but received %d.", argc));

	os = new OS_OpenHarmony();

	NativeResourceManager *resource_manager = OH_ResourceManager_InitNativeResourceManager(p_env, napi_args[0]);
	OS_OpenHarmony::get_singleton()->set_native_resource_manager(resource_manager);

	String content;
	FileAccessOpenHarmony::get_rawfile_content("_cl_", content);

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

	bool failed = Main::setup(OS::get_singleton()->get_executable_path().utf8().get_data(), args.size(), (char **)cmdline, false) != OK;

	if (cmdline) {
		for (int i = 0; i < args.size(); i++) {
			memfree((void *)cmdline[i]);
		}
		memfree(cmdline);
	}

	if (failed) {
		delete os;
		os = nullptr;
		return _bool_to_value(p_env, false);
	}

	initialized = true;
	return _bool_to_value(p_env, true);
}

napi_value NAPIBridge::finalize(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!initialized, _bool_to_value(p_env, false), "Finalize failed, not yet initialized.");
	stop(p_env, p_info);
	Main::cleanup();
	if (os) {
		delete os;
		os = nullptr;
	}
	initialized = false;
	return _bool_to_value(p_env, true);
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

	OS_OpenHarmony::get_singleton()->set_native_main_window_id(window_id);
	return _bool_to_value(p_env, started);
}

napi_value NAPIBridge::is_started(napi_env__ *p_env, napi_callback_info p_info) {
	return _bool_to_value(p_env, started);
}

napi_value NAPIBridge::iteration(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Iteration failed, not yet started.");
	DisplayServer::get_singleton()->process_events();
	return _bool_to_value(p_env, Main::iteration());
}

napi_value NAPIBridge::stop(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Stop failed, not yet started.");
	OS::get_singleton()->get_main_loop()->finalize();
	started = false;
	return nullptr;
}

napi_value NAPIBridge::focus_out(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Focus out failed, not yet started.");
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
	}
	return nullptr;
}
napi_value NAPIBridge::focus_in(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Focus in failed, not yet started.");
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
	}
	return nullptr;
}
napi_value NAPIBridge::pause(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Pause failed, not yet started.");
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
	}
	return nullptr;
}
napi_value NAPIBridge::resume(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_COND_V_MSG(!started, nullptr, "Resume failed, not yet started.");
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
	}
	return nullptr;
}

void NAPIBridge::_setup_engine(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_COND_MSG(started, "Can only be started once.");

	OHNativeWindow *native_window = OH_ArkUI_XComponent_GetNativeWindow(p_holder);
	OS_OpenHarmony::get_singleton()->set_native_window(native_window);

	Error err = Main::setup2();
	ERR_FAIL_COND_MSG(err != OK, vformat("The second stage of setup failed, err: %d.", err));

	int error = Main::start();
	ERR_FAIL_COND_MSG(error != EXIT_SUCCESS, vformat("Main start failed, err: %d.", error));

	OS::get_singleton()->get_main_loop()->initialize();
	started = true;
}

void _register_node_event(ArkUI_NodeHandle p_node, ArkUI_NodeEventType p_event_type, int32_t p_target_id, void *p_user_data) {
	int32_t err = node_api->registerNodeEvent(p_node, p_event_type, p_target_id, p_user_data);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("Register node event failed, event type: %d, err: %d.", p_event_type, err));
}

void _set_attribute(ArkUI_Node *p_node, ArkUI_NodeAttributeType p_attribute, const ArkUI_AttributeItem *p_item) {
	int32_t err = node_api->setAttribute(p_node, p_attribute, p_item);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("Set attribute failed, attribute id: %d, err: %d", p_attribute, err));
}

ArkUI_Node *NAPIBridge::_create_xc(int32_t p_window_id) {
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

	OH_ArkUI_XComponent_RegisterOnFrameCallback(xc, DisplayServerOpenHarmony::_frame_callback_native);

	int32_t err = node_api->addNodeEventReceiver(xc, DisplayServerOpenHarmony::_input);
	if (err != ARKUI_ERROR_CODE_NO_ERROR) {
		ERR_PRINT(vformat("addNodeEventReceiver failed, err: %d.", err));
	}
	_register_node_event(xc, NODE_TOUCH_EVENT, p_window_id, nullptr);
	// _register_node_event(xc, NODE_ON_CLICK_EVENT, p_window_id, nullptr);
	_register_node_event(xc, NODE_ON_MOUSE, p_window_id, nullptr);
	_register_node_event(xc, NODE_ON_AXIS, p_window_id, nullptr);
	_register_node_event(xc, NODE_ON_KEY_EVENT, p_window_id, nullptr);

	return xc;
}

void NAPIBridge::_node_content_callback(ArkUI_NodeContentEvent *p_event) {
	ERR_FAIL_NULL(p_event);

	ArkUI_NodeContentHandle node = OH_ArkUI_NodeContentEvent_GetNodeContentHandle(p_event);
	ERR_FAIL_NULL(node);

	int32_t *window_id_ptr = reinterpret_cast<int32_t *>(OH_ArkUI_NodeContent_GetUserData(node));
	ERR_FAIL_NULL(window_id_ptr);

	switch (OH_ArkUI_NodeContentEvent_GetEventType(p_event)) {
		case NODE_CONTENT_EVENT_ON_ATTACH_TO_WINDOW: {
			DisplayServerOpenHarmony::NodeData *nd = memnew(DisplayServerOpenHarmony::NodeData);
			nd->window_id = *window_id_ptr;
			nd->parent = node;

			ArkUI_Node *xc = _create_xc(nd->window_id);
			if (!xc) {
				node_api->removeNodeEventReceiver(nd->node, DisplayServerOpenHarmony::_input);
				memdelete(nd);
				node_api->disposeNode(nd->node);
				ERR_PRINT_ONCE("Creating xc failed.");
				return;
			}
			nd->node = xc;
			OH_ArkUI_SurfaceHolder *holder = OH_ArkUI_SurfaceHolder_Create(xc);
			nd->holder = holder;
			OH_ArkUI_SurfaceCallback *callback = OH_ArkUI_SurfaceCallback_Create();
			nd->callback = callback;

			OH_ArkUI_SurfaceCallback_SetSurfaceCreatedEvent(callback, DisplayServerOpenHarmony::_surface_created_native);
			OH_ArkUI_SurfaceCallback_SetSurfaceChangedEvent(callback, DisplayServerOpenHarmony::_surface_changed_native);
			OH_ArkUI_SurfaceCallback_SetSurfaceDestroyedEvent(callback, DisplayServerOpenHarmony::_surface_destroyed_native);
			OH_ArkUI_SurfaceCallback_SetSurfaceShowEvent(callback, DisplayServerOpenHarmony::_surface_show_native);
			OH_ArkUI_SurfaceCallback_SetSurfaceHideEvent(callback, DisplayServerOpenHarmony::_surface_hide_native);
			OH_ArkUI_SurfaceHolder_AddSurfaceCallback(holder, callback);

			if (nd->parent) {
				int32_t res = OH_ArkUI_NodeContent_AddNode(nd->parent, nd->node);
				ERR_FAIL_COND_MSG(res != ARKUI_ERROR_CODE_NO_ERROR, vformat("OH_ArkUI_NodeContent_AddNode failed, err: %d.", res));
			}

			if (nd->window_id == OS_OpenHarmony::get_singleton()->get_native_main_window_id()) {
				_setup_engine(nd->holder);
			}

			if (!started) {
				node_api->removeNodeEventReceiver(nd->node, DisplayServerOpenHarmony::_input);
				memdelete(nd);
				node_api->disposeNode(nd->node);
				ERR_PRINT_ONCE("Creating native node and starting Godot failed.");
				return;
			}
			DisplayServerOpenHarmony::get_singleton()->window_node_map[nd->window_id] = nd;
			DisplayServerOpenHarmony::get_singleton()->node_datas[nd->node] = nd;
		} break;
		case NODE_CONTENT_EVENT_ON_DETACH_FROM_WINDOW: {
			int32_t window_id = *window_id_ptr;
			memdelete(window_id_ptr);
			OH_ArkUI_NodeContent_SetUserData(node, nullptr);
			ERR_FAIL_COND_MSG(!started, "Godot was not successfully started.");

			HashMap<int32_t, DisplayServerOpenHarmony::NodeData *>::Iterator I = DisplayServerOpenHarmony::get_singleton()->window_node_map.find(window_id);
			ERR_FAIL_NULL(I);

			DisplayServerOpenHarmony::NodeData *nd = I->value;
			DisplayServerOpenHarmony::get_singleton()->window_node_map.remove(I);
			DisplayServerOpenHarmony::get_singleton()->node_datas.erase(nd->node);
			node_api->removeNodeEventReceiver(nd->node, DisplayServerOpenHarmony::_input);
			memdelete(nd);
			node_api->disposeNode(nd->node);
		} break;
	}
}

void NAPIBridge::_create_native_node(ArkUI_NodeContent *p_node, int32_t p_native_window_id) {
	int32_t *window_id_ptr = memnew(int32_t);
	*window_id_ptr = p_native_window_id;
	int32_t err = OH_ArkUI_NodeContent_SetUserData(p_node, window_id_ptr);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("OH_ArkUI_NodeContent_SetUserData failed, error code: %d.", err));

	ERR_FAIL_NULL(node_api);
	ERR_FAIL_NULL(node_api->createNode);
	ERR_FAIL_NULL(node_api->addChild);

	err = OH_ArkUI_NodeContent_RegisterCallback(p_node, _node_content_callback);
	ERR_FAIL_COND_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, vformat("OH_ArkUI_NodeContent_RegisterCallback failed, error code: %d.", err));
}

napi_value NAPIBridge::create_native_node(napi_env__ *p_env, napi_callback_info p_info) {
	ERR_FAIL_NULL_V(p_env, nullptr);
	ERR_FAIL_NULL_V(p_info, nullptr);
	size_t argc = 2;
	napi_value args[2] = { nullptr, nullptr };
	napi_status status = napi_get_cb_info(p_env, p_info, &argc, args, nullptr, nullptr);
	ERR_FAIL_COND_V_MSG(status != napi_ok, nullptr, vformat("napi_get_cb_info failed, error code: %d.", status));
	ERR_FAIL_COND_V_MSG(argc != 2, nullptr, vformat("Incorrect number of arguments: expected 2 but received %d.", argc));

	int32_t window_id = -1;
	status = napi_get_value_int32(p_env, args[0], &window_id);
	ERR_FAIL_COND_V_MSG(status != napi_ok, nullptr, vformat("napi_get_value_int32 failed, error code: %d.", status));

	ArkUI_NodeContentHandle node = nullptr;
	int32_t err = OH_ArkUI_GetNodeContentFromNapiValue(p_env, args[1], &node);
	ERR_FAIL_COND_V_MSG(err != ARKUI_ERROR_CODE_NO_ERROR, nullptr, vformat("OH_ArkUI_GetNodeContentFromNapiValue failed, error code: %d.", err));
	ERR_FAIL_NULL_V(node, nullptr);

	_create_native_node(node, window_id);
	return nullptr;
}

NAPIBridge::NAPIBridge() {}

NAPIBridge::~NAPIBridge() {}
