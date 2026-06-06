/**************************************************************************/
/*  display_server_openharmony.cpp                                        */
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

#include "display_server_openharmony.h"

#include "key_mapping_openharmony.h"
#include "napi_bridge.h"
#include "os_openharmony.h"
#include "pixelmap_driver.h"
#include "rendering_context_driver_vulkan_openharmony.h"
#include "wrapper_openharmony.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/math/vector2i.h"
#include "core/object/callable_mp.h"
#include "main/main.h"
#include "servers/display/native_menu.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"

#include <arkui/native_key_event.h>
#include <database/pasteboard/oh_pasteboard.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <window_manager/oh_display_capture.h>
#include <window_manager/oh_display_manager.h>

int DisplayServerOpenHarmony::main_native_window_id = -1;
NativeWindow *DisplayServerOpenHarmony::main_native_window = nullptr;

void DisplayServerOpenHarmony::_dispatch_input_event(const Ref<InputEvent> &p_event) {
	Ref<InputEventFromWindow> event_from_window = p_event;

	if (event_from_window.is_valid()) {
		DisplayServerEnums::WindowID window_id = event_from_window->get_window_id();

		Ref<InputEventKey> key_event = p_event;
		// if (!popup_menu_list.is_empty() && key_event.is_valid()) {
		// 	// Redirect to the highest popup menu.
		// 	window_id = popup_menu_list.back()->get();
		// }

		// Send to a single window.
		if (windows.has(window_id)) {
			Ref<InputEventMouse> iev_mouse = p_event;
			if (iev_mouse.is_valid()) {
				mouse_pos = iev_mouse->get_position() + windows[window_id].rect.position;
			}

			Callable callable = windows[window_id].input_event_callback;
			if (callable.is_valid()) {
				callable.call(p_event);
			}
		}
	} else {
		// Send to all windows. Copy all pending callbacks, since callback can erase window.
		Vector<Callable> cbs;
		for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
			Callable callable = E.value.input_event_callback;
			if (callable.is_valid()) {
				cbs.push_back(callable);
			}
		}

		for (const Callable &cb : cbs) {
			cb.call(p_event);
		}
	}
}

void DisplayServerOpenHarmony::dispatch_input_events(const Ref<InputEvent> &p_event) {
	static_cast<DisplayServerOpenHarmony *>(get_singleton())->_dispatch_input_event(p_event);
}

void DisplayServerOpenHarmony::_update_window_rect(const Rect2i &p_rect, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	WindowData &wd = windows[p_window];

	if (wd.rect == p_rect) {
		return;
	}

	wd.rect = p_rect;

#ifdef RD_ENABLED
	if (wd.visible && rendering_context) {
		rendering_context->window_set_size(p_window, wd.rect.size.width, wd.rect.size.height);
	}
#endif

	if (wd.rect_changed_callback.is_valid()) {
		wd.rect_changed_callback.call(wd.rect);
	}
}

void DisplayServerOpenHarmony::_parse_modifiers_from(const ArkUI_UIInputEvent *p_source_event, const Ref<InputEventWithModifiers> &p_event) {
	uint64_t modifier_keys = 0;
	OH_ArkUI_UIInputEvent_GetModifierKeyStates(p_source_event, &modifier_keys);

	p_event->set_ctrl_pressed(modifier_keys & ARKUI_MODIFIER_KEY_CTRL);
	p_event->set_shift_pressed(modifier_keys & ARKUI_MODIFIER_KEY_SHIFT);
	p_event->set_alt_pressed(modifier_keys & ARKUI_MODIFIER_KEY_ALT);

	int32_t pressed_keys[10] = {};
	int32_t length = 10;
	OH_ArkUI_UIInputEvent_GetPressedKeys(p_source_event, pressed_keys, &length);
	length = MIN(10, length);
	for (int i = 0; i < length; i++) {
		if (KeyMappingOpenHarmony::map_key(pressed_keys[i]) == Key::META) {
			p_event->set_meta_pressed(true);
			return;
		}
	}
}

MouseButton DisplayServerOpenHarmony::_map_mouse_button(int32_t p_button) {
	switch (p_button) {
		case UI_MOUSE_EVENT_BUTTON_LEFT:
			return MouseButton::LEFT;
		case UI_MOUSE_EVENT_BUTTON_RIGHT:
			return MouseButton::RIGHT;
		case UI_MOUSE_EVENT_BUTTON_MIDDLE:
			return MouseButton::MIDDLE;
		case UI_MOUSE_EVENT_BUTTON_BACK:
			return MouseButton::MB_XBUTTON1;
		case UI_MOUSE_EVENT_BUTTON_FORWARD:
			return MouseButton::MB_XBUTTON2;
		default:
			return MouseButton::NONE;
	}
}

void DisplayServerOpenHarmony::_parse_mouse_event_from(ArkUI_UIInputEvent *p_source_event, const Ref<InputEventMouse> &p_event) {
	// Button masks.
	int32_t pressed_buttons[10] = {};
	int32_t length = 10;
	OH_ArkUI_MouseEvent_GetPressedButtons(p_source_event, pressed_buttons, &length);
	length = MIN(10, length);
	for (int i = 0; i < length; i++) {
		MouseButton button_idx = _map_mouse_button(pressed_buttons[i]);
		p_event->set_button_mask(mouse_button_to_mask(button_idx));
	}

	// Position.
	Point2 pos;
	pos.x = OH_ArkUI_PointerEvent_GetX(p_source_event);
	pos.y = OH_ArkUI_PointerEvent_GetY(p_source_event);
	pos = vp_to_px(pos, _get_screen_index(DisplayServerEnums::SCREEN_OF_MAIN_WINDOW));
	p_event->set_position(pos);
	p_event->set_global_position(pos);

	// Device and window.
	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(p_source_event);
	p_event->set_device(device_id);
	p_event->set_window_id(0);

	// Modifiers.
	_parse_modifiers_from(p_source_event, p_event);
}

void DisplayServerOpenHarmony::_parse_touch_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
	Ref<InputEventScreenTouch> ste;
	ste.instantiate();
	// ste->set_device(p_device);
	ste->set_window_id(p_window);

	uint32_t id = 0;
	OH_ArkUI_PointerEvent_GetChangedPointerId(p_event, &id);
	ste->set_index(id);

	const int32_t action = OH_ArkUI_UIInputEvent_GetAction(p_event);
	const bool pressed = action == UI_TOUCH_EVENT_ACTION_DOWN;
	const bool canceled = action == UI_TOUCH_EVENT_ACTION_CANCEL;
	ste->set_pressed(pressed);
	ste->set_canceled(canceled);

	Point2 pos;
	pos.x = OH_ArkUI_PointerEvent_GetX(p_event);
	pos.y = OH_ArkUI_PointerEvent_GetY(p_event);
	// ERR_PRINT(vformat("type:%d, action:%d, position:%v.", event_type, action, pos));

	pos = vp_to_px(pos, _get_screen_index(DisplayServerEnums::SCREEN_OF_MAIN_WINDOW));
	ste->set_position(pos);

	if (pressed) {
		const uint32_t pointer_count = OH_ArkUI_PointerEvent_GetPointerCount(p_event);
		if (pointer_count == 1) {
			const int64_t time = OH_ArkUI_UIInputEvent_GetEventTime(p_event) / 1000000LL;
			const int64_t diff = time - last_click_ms;

			if (diff < 400 && last_click_pos.distance_to(pos) < 5) {
				last_click_ms = 0;
				last_click_pos = Point2i(-100, -100);
				ste->set_double_tap(true);
			} else {
				last_click_ms = time;
				last_click_pos = pos;
			}
		}
	}
	// ERR_PRINT(vformat("type:%d, action:%d, position:%v.", event_type, action, pos));

	Input::get_singleton()->parse_input_event(ste);
}

void DisplayServerOpenHarmony::_parse_axis_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
	const int32_t source_type = OH_ArkUI_UIInputEvent_GetSourceType(p_event);
	if (source_type == UI_INPUT_EVENT_SOURCE_TYPE_MOUSE) {
		const float angle = OH_ArkUI_AxisEvent_GetVerticalAxisValue(p_event);
		if (Math::is_zero_approx(angle)) {
			return;
		}

		Ref<InputEventMouseButton> mb;
		mb.instantiate();
		// mb->set_device(p_device);
		mb->set_window_id(p_window);

		if (angle > 0) {
			mb->set_button_index(MouseButton::WHEEL_UP);
		} else {
			mb->set_button_index(MouseButton::WHEEL_DOWN);
		}

		_parse_mouse_event_from(p_event, mb);

		Input::get_singleton()->parse_input_event(mb);
	}
}

void DisplayServerOpenHarmony::_parse_mouse_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
	const int32_t button = OH_ArkUI_MouseEvent_GetMouseButton(p_event);
	if (button == UI_MOUSE_EVENT_BUTTON_LEFT) {
		return; // The left mouse button event will also be converted into a touch event.
	}
	const int32_t mouse_action = OH_ArkUI_MouseEvent_GetMouseAction(p_event);
	if (mouse_action == UI_MOUSE_EVENT_ACTION_UNKNOWN) {
		return;
	}

	Ref<InputEventMouse> me;
	MouseButton button_index = _map_mouse_button(button);

	if (mouse_action == UI_MOUSE_EVENT_ACTION_MOVE) {
		Ref<InputEventMouseMotion> mm;
		mm.instantiate();
		me = mm;
	} else {
		ERR_FAIL_COND(button_index == MouseButton::NONE);

		Ref<InputEventMouseButton> mb;
		mb.instantiate();
		me = mb;

		mb->set_button_index(button_index);
		mb->set_pressed(mouse_action == UI_MOUSE_EVENT_ACTION_PRESS);
		mb->set_canceled(mouse_action == UI_MOUSE_EVENT_ACTION_CANCEL);
	}

	// me->set_device(p_device);
	me->set_window_id(p_window);

	BitField<MouseButtonMask> button_mask = MouseButtonMask::NONE;
	if (button_index != MouseButton::NONE) {
		button_mask = mouse_button_to_mask(button_index);
	}
	me->set_button_mask(button_mask);
	_parse_mouse_event_from(p_event, me); // Additional mask.

	Input::get_singleton()->parse_input_event(me);
}

void DisplayServerOpenHarmony::_parse_key_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
	const ArkUI_KeySourceType source_type = OH_ArkUI_KeyEvent_GetKeySource(p_event);
	if (source_type == ARKUI_KEY_SOURCE_TYPE_KEYBOARD) {
		const ArkUI_KeyEventType key_type = OH_ArkUI_KeyEvent_GetType(p_event);
		if (key_type != ARKUI_KEY_EVENT_DOWN && key_type != ARKUI_KEY_EVENT_UP && key_type != ARKUI_KEY_EVENT_LONG_PRESS) {
			return;
		}

		Ref<InputEventKey> ke;
		ke.instantiate();
		// ke->set_device(p_device);
		ke->set_window_id(p_window);

		ke->set_pressed(key_type != ARKUI_KEY_EVENT_UP);
		ke->set_echo(key_type == ARKUI_KEY_EVENT_LONG_PRESS);

		const int32_t keysym = OH_ArkUI_KeyEvent_GetKeyCode(p_event);
		ke->set_keycode(KeyMappingOpenHarmony::map_key(keysym));
		ke->set_location(KeyMappingOpenHarmony::get_location(keysym));

		const uint32_t unicode = OH_ArkUI_KeyEvent_GetUnicode(p_event);
		ke->set_unicode(unicode);

		_parse_modifiers_from(p_event, ke);

		Input::get_singleton()->parse_input_event(ke);
	}
}

void DisplayServerOpenHarmony::node_event_receiver(ArkUI_NodeEvent *p_event) {
	ERR_FAIL_NULL(get_singleton());
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_NodeEvent_GetUserData(p_event));
	ERR_FAIL_NULL(nd);
	ArkUI_UIInputEvent *source_event = OH_ArkUI_NodeEvent_GetInputEvent(p_event);
	ERR_FAIL_NULL(source_event);

	const int32_t type = OH_ArkUI_UIInputEvent_GetType(source_event);
	if (type == ARKUI_UIINPUTEVENT_TYPE_UNKNOWN) {
		return;
	}
	const int32_t display_id = OH_ArkUI_UIInputEvent_GetTargetDisplayId(source_event);
	const int32_t native_window_id = OH_ArkUI_NodeEvent_GetTargetId(p_event);
	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(source_event);

	const ArkUI_NodeEventType event_type = OH_ArkUI_NodeEvent_GetEventType(p_event);

	ERR_PRINT(vformat("Display id:%d, native_window_id:%d, window:%d, device:%d, type:%d, event type:%d", display_id, native_window_id, nd->window_id, device_id, type, event_type));

	if (event_type == NODE_TOUCH_EVENT && type == ARKUI_UIINPUTEVENT_TYPE_TOUCH) {
		get_singleton()->_parse_touch_event(source_event, device_id, nd->window_id);
	} else if (event_type == NODE_ON_AXIS && type == ARKUI_UIINPUTEVENT_TYPE_AXIS) {
		get_singleton()->_parse_axis_event(source_event, device_id, nd->window_id);
	} else if (event_type == NODE_ON_MOUSE && type == ARKUI_UIINPUTEVENT_TYPE_MOUSE) {
		get_singleton()->_parse_mouse_event(source_event, device_id, nd->window_id);
	} else if (event_type == NODE_ON_KEY_EVENT && type == ARKUI_UIINPUTEVENT_TYPE_KEY) {
		get_singleton()->_parse_key_event(source_event, device_id, nd->window_id);
	}
}

void DisplayServerOpenHarmony::_surface_created(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(p_holder);
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);
	ERR_FAIL_COND(nd->window_id != -1);
	nd->window_id = window_id_counter++;
	ERR_FAIL_COND_MSG(!windows.has(nd->window_id), vformat("The window %d does exist.", nd->window_id));
	WindowData &wd = windows[nd->window_id];

	wd.native_window = OH_ArkUI_XComponent_GetNativeWindow(p_holder);
	wd.native_window_id = nd->native_window_id;

	show_window(wd.id);
}

void DisplayServerOpenHarmony::_surface_changed(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height) {
	ERR_FAIL_NULL(p_holder);
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);
	window_set_size(Size2i(p_width, p_height), nd->window_id);
}

void DisplayServerOpenHarmony::_surface_destroyed(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(p_holder);
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);
}

void DisplayServerOpenHarmony::_surface_show(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(p_holder);
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);

	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
	}
}

void DisplayServerOpenHarmony::_surface_hide(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(p_holder);
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);

	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
	}
}

void DisplayServerOpenHarmony::surface_created_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_PRINT(vformat("surface_created_callback %d", main_native_window_id));
	if (get_singleton()) {
		get_singleton()->_surface_created(p_holder);
		return;
	}
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);
	ERR_FAIL_COND(nd->window_id != -1);

	DisplayServerOpenHarmony::main_native_window = OH_ArkUI_XComponent_GetNativeWindow(p_holder);
	DisplayServerOpenHarmony::main_native_window_id = nd->native_window_id;

	ERR_PRINT(vformat("surface_created_callback 003, %d, %d", nd->native_window_id, DisplayServerOpenHarmony::main_native_window_id));
	NAPIBridge::_setup_engine(nd->holder);
	ERR_PRINT(vformat("surface_created_callback 004, %d, %d", nd->native_window_id, DisplayServerOpenHarmony::main_native_window_id));
	nd->window_id = get_singleton()->window_id_counter++;
}

void DisplayServerOpenHarmony::surface_changed_callback(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height) {
	ERR_PRINT("surface_changed_callback");
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_changed(p_holder, p_width, p_height);
}

void DisplayServerOpenHarmony::surface_destroyed_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_PRINT("surface_destroyed_callback");
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_destroyed(p_holder);
}

void DisplayServerOpenHarmony::surface_show_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_PRINT("surface_show_callback");
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_show(p_holder);
}

void DisplayServerOpenHarmony::surface_hide_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_PRINT("surface_hide_callback");
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_hide(p_holder);
}

void DisplayServerOpenHarmony::frame_callback(ArkUI_NodeHandle p_node, uint64_t p_timestamp, uint64_t p_target_timestamp) {
	if (get_singleton()->exited) {
		return;
	}
	DisplayServer::get_singleton()->process_events();
	if (Main::iteration()) {
		get_singleton()->exited = true;
		NAPIBridge::get_singleton()->terminate_self_ability();
	}
}

DisplayServerOpenHarmony *DisplayServerOpenHarmony::get_singleton() {
	return static_cast<DisplayServerOpenHarmony *>(DisplayServer::get_singleton());
}

Vector<String> DisplayServerOpenHarmony::get_rendering_drivers_func() {
	Vector<String> drivers;
	drivers.push_back("vulkan");
	return drivers;
}

DisplayServer *DisplayServerOpenHarmony::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerOpenHarmony(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		OS::get_singleton()->alert(
				"Your device seems not to support the required Vulkan version.\n\n"
				"Unable to initialize Vulkan video driver.");
	}
	return ds;
}

void DisplayServerOpenHarmony::register_openharmony_driver() {
	register_create_function("openharmony", create_func, get_rendering_drivers_func);
}

DisplayServerOpenHarmony::DisplayServerOpenHarmony(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	KeyMappingOpenHarmony::initialize();

	native_menu = memnew(NativeMenu);

	{
		NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_RegisterDisplayAddListener(display_add_or_change_callback, &screen_add_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to register display add listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_RegisterDisplayChangeListener(display_add_or_change_callback, &screen_change_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to register display change listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_RegisterDisplayRemoveListener(display_remove_callback, &screen_remove_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to register display remove listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_RegisterAvailableAreaChangeListener(available_area_change_callback, &available_area_change_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to register available area change listener, error: %d.", err));
		}

		foldable = OH_NativeDisplayManager_IsFoldable();

		if (foldable) {
			err = OH_NativeDisplayManager_RegisterFoldDisplayModeChangeListener(fold_display_mode_change_callback, &fold_display_mode_change_listener_idx);
			if (err != DISPLAY_MANAGER_OK) {
				ERR_PRINT(vformat("Failed to register fold display mode change listener, error: %d.", err));
			}

			NativeDisplayManager_FoldDisplayMode native_fold_display_mode;
			OH_NativeDisplayManager_GetFoldDisplayMode(&native_fold_display_mode);
			fold_display_mode = (FoldDisplayMode)native_fold_display_mode;
		}

		_update_all_screen_info();
	}

	context = p_context;

	r_error = ERR_UNAVAILABLE;

	rendering_driver = p_rendering_driver;
	bool driver_found = false;

	// if (rendering_driver == "dummy") {
	// 	RasterizerDummy::make_current();
	// 	driver_found = true;
	// }

#ifdef RD_ENABLED
#ifdef VULKAN_ENABLED
	if (rendering_driver == "vulkan") {
		rendering_context = memnew(RenderingContextDriverVulkanOpenHarmony);
	}
#endif // VULKAN_ENABLED

	if (rendering_context) {
		if (rendering_context->initialize() != OK) {
			ERR_PRINT(vformat("Failed to initialize %s context.", rendering_driver));
			memdelete(rendering_context);
			rendering_context = nullptr;
			r_error = ERR_CANT_CREATE;
			ERR_FAIL_MSG("Could not initialize " + rendering_driver);
		}
		driver_found = true;
	}
#endif // RD_ENABLED

	if (!driver_found) {
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG("Video driver not found.");
	}

	WindowData &wd = windows[DisplayServerEnums::MAIN_WINDOW_ID];

	wd.id = window_id_counter;
	wd.mode = p_mode;
	wd.flags = p_flags;
	wd.vsync_mode = p_vsync_mode;
	wd.rect.size = p_resolution;
	wd.title = "Godot";
	wd.native_window = main_native_window;
	wd.native_window_id = main_native_window_id;

	show_window(wd.id);

	if (!windows.has(DisplayServerEnums::MAIN_WINDOW_ID) || !windows[DisplayServerEnums::MAIN_WINDOW_ID].visible) {
		ERR_PRINT("Could not map the main window.");
		r_error = ERR_CANT_CREATE;
		return;
	}

#ifdef RD_ENABLED
	if (rendering_context) {
		rendering_device = memnew(RenderingDevice);
		Error err = rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID);
		if (err != OK) {
			ERR_PRINT(vformat("Failed to initialize rendering device. error: %d.", err));
			memdelete(rendering_device);
			rendering_device = nullptr;
			memdelete(rendering_context);
			rendering_context = nullptr;
			r_error = err;
			return;
		}
		rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

		RendererCompositorRD::make_current();
	}
#endif // RD_ENABLED

	cursor_set_shape(DisplayServerEnums::CURSOR_BUSY);
	Input::get_singleton()->set_event_dispatch_function(dispatch_input_events);

	ohos_wrapper_screen_set_keep_on(wd.native_window_id, GLOBAL_GET("display/window/energy_saving/keep_screen_on"));

	r_error = OK;
}

DisplayServerOpenHarmony::~DisplayServerOpenHarmony() {
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

	virtual_keyboard_hide(); // Ensure IME resources are cleaned up.

	for (KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
#ifdef RD_ENABLED
		if (rendering_device) {
			rendering_device->screen_free(E.key);
		}

		if (rendering_context) {
			rendering_context->window_destroy(E.key);
		}
#endif // RD_ENABLED
	}

#ifdef RD_ENABLED
	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}

	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
#endif // RD_ENABLED

	if (custom_cursor) {
		OH_PixelmapNative *pixelmap = nullptr;
		OH_Input_CustomCursor_GetPixelMap(custom_cursor, &pixelmap);
		OH_Input_CustomCursor_Destroy(&custom_cursor);
		OH_PixelmapNative_Destroy(&pixelmap);
		custom_cursor = nullptr;
	}

	virtual_keyboard_hide();

	{
		NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_UnregisterDisplayAddListener(screen_add_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to unregister display add listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_UnregisterDisplayChangeListener(screen_change_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to unregister display change listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_UnregisterDisplayRemoveListener(screen_remove_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to unregister display remove listener, error: %d.", err));
		}

		err = OH_NativeDisplayManager_UnregisterAvailableAreaChangeListener(available_area_change_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to unregister available area change listener, error: %d.", err));
		}

		if (foldable) {
			err = OH_NativeDisplayManager_UnregisterFoldDisplayModeChangeListener(fold_display_mode_change_listener_idx);
			if (err != DISPLAY_MANAGER_OK) {
				ERR_PRINT(vformat("Failed to unregister fold display mode change listener, error: %d.", err));
			}
		}
	}
}

void DisplayServerOpenHarmony::_window_callback(const Callable &p_callable, const Variant &p_arg, bool p_deferred) const {
	if (p_callable.is_valid()) {
		if (p_deferred) {
			p_callable.call_deferred(p_arg);
		} else {
			p_callable.call(p_arg);
		}
	}
}

bool DisplayServerOpenHarmony::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_MOUSE:
		case DisplayServerEnums::FEATURE_CLIPBOARD:
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
		case DisplayServerEnums::FEATURE_CURSOR_SHAPE:
		case DisplayServerEnums::FEATURE_CUSTOM_CURSOR_SHAPE:
		case DisplayServerEnums::FEATURE_IME:
		case DisplayServerEnums::FEATURE_KEEP_SCREEN_ON:
		case DisplayServerEnums::FEATURE_SCREEN_CAPTURE:
			return true;
		default:
			return false;
	}
}

String DisplayServerOpenHarmony::get_name() const {
	return "OpenHarmony";
}

Vector2 DisplayServerOpenHarmony::vp_to_px(Vector2 p_vp, int p_screen) {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Vector2());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Vector2(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return p_vp * I->value.density_pixels;
}

void DisplayServerOpenHarmony::_update_display_info_from(ScreenInfo &p_screen, NativeDisplayManager_DisplayInfo *p_native_display) {
	p_screen.id = p_native_display->id;
	p_screen.name = p_native_display->name;
	p_screen.alive = p_native_display->isAlive;
	p_screen.size = Size2i(p_native_display->width, p_native_display->height);
	p_screen.physical_size = Size2i(p_native_display->physicalWidth, p_native_display->physicalHeight);
	p_screen.refresh_rate = p_native_display->refreshRate;
	p_screen.available_size = Size2i(p_native_display->availableWidth, p_native_display->availableHeight);
	p_screen.density_dpi = p_native_display->densityDPI;
	p_screen.density_pixels = p_native_display->densityPixels;
	p_screen.scaled_density = p_native_display->scaledDensity;
	p_screen.dpi = Vector2(p_native_display->xDPI, p_native_display->yDPI);
	p_screen.rotation = (ScreenRotation)p_native_display->rotation;
	p_screen.state = (ScreenState)p_native_display->state;
	switch (p_native_display->orientation) {
		case DISPLAY_MANAGER_PORTRAIT: {
			p_screen.orientation = DisplayServerEnums::SCREEN_PORTRAIT;
		} break;
		case DISPLAY_MANAGER_LANDSCAPE: {
			p_screen.orientation = DisplayServerEnums::SCREEN_LANDSCAPE;
		} break;
		case DISPLAY_MANAGER_PORTRAIT_INVERTED: {
			p_screen.orientation = DisplayServerEnums::SCREEN_REVERSE_PORTRAIT;
		} break;
		case DISPLAY_MANAGER_LANDSCAPE_INVERTED: {
			p_screen.orientation = DisplayServerEnums::SCREEN_REVERSE_LANDSCAPE;
		} break;
		default: {
			p_screen.orientation = DisplayServerEnums::SCREEN_PORTRAIT;
		} break;
	}
}

void DisplayServerOpenHarmony::_update_extra_info(ScreenInfo &p_screen_info) {
	NativeDisplayManager_Rect *rect = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateAvailableArea(p_screen_info.id, &rect);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to create available area for id: %d, error: %d.", p_screen_info.id, err));
	} else {
		p_screen_info.available_rect = Rect2i(rect->left, rect->top, rect->width, rect->height);
		OH_NativeDisplayManager_DestroyAvailableArea(rect);
	}

	NativeDisplayManager_SourceMode source_mode;
	err = OH_NativeDisplayManager_GetDisplaySourceMode(p_screen_info.id, &source_mode);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to fetch display source mode for id: %d, error: %d.", p_screen_info.id, err));
	} else {
		p_screen_info.source_mode = (DisplaySourceMode)source_mode;
	}

	Point2i pos;
	if (source_mode == NativeDisplayManager_SourceMode::DISPLAY_SOURCE_MODE_MAIN || source_mode == NativeDisplayManager_SourceMode::DISPLAY_SOURCE_MODE_EXTEND) {
		err = OH_NativeDisplayManager_GetDisplayPosition(p_screen_info.id, &pos.x, &pos.y);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to fetch display position for id: %d, error: %d.", p_screen_info.id, err));
		}
	}
	p_screen_info.global_position = pos;
}

void DisplayServerOpenHarmony::_update_all_screen_info() {
	screens.clear();
	screen_ids.clear();

	NativeDisplayManager_DisplaysInfo *native_displays = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateAllDisplays(&native_displays);
	ERR_FAIL_COND_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to retrieve display information, error: %d.", err));

	screen_ids.resize(native_displays->displaysLength);
	for (uint32_t i = 0; i < native_displays->displaysLength; ++i) {
		NativeDisplayManager_DisplayInfo *native_display = &(native_displays->displaysInfo[i]);
		ScreenInfo &screen = screens[native_display->id];
		screen_ids.write[i] = native_display->id;

		_update_display_info_from(screen, native_display);
		_update_extra_info(screen);
	}
	OH_NativeDisplayManager_DestroyAllDisplays(native_displays);
}

void DisplayServerOpenHarmony::display_add_or_change_callback(uint64_t p_display_id) {
	NativeDisplayManager_DisplayInfo *native_display = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateDisplayById(p_display_id, &native_display);
	ERR_FAIL_COND_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to create display for id: %d, error: %d.", p_display_id, err));

	if (!get_singleton()->screens.has(p_display_id)) {
		get_singleton()->screen_ids.push_back(p_display_id);
	}

	ScreenInfo &screen = get_singleton()->screens[p_display_id];
	get_singleton()->_update_display_info_from(screen, native_display);
	get_singleton()->_update_extra_info(screen);

	OH_NativeDisplayManager_DestroyDisplay(native_display);
}

void DisplayServerOpenHarmony::display_remove_callback(uint64_t p_display_id) {
	HashMap<uint64_t, ScreenInfo>::Iterator I = get_singleton()->screens.find(p_display_id);
	ERR_FAIL_NULL_MSG(I, vformat("The display %d wasn't found.", p_display_id));
	get_singleton()->screens.remove(I);
	get_singleton()->screen_ids.erase(p_display_id);
}

void DisplayServerOpenHarmony::available_area_change_callback(uint64_t p_display_id) {
	HashMap<uint64_t, ScreenInfo>::Iterator I = get_singleton()->screens.find(p_display_id);
	ERR_FAIL_NULL_MSG(I, vformat("The display %d wasn't found.", p_display_id));

	NativeDisplayManager_Rect *rect = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateAvailableArea(p_display_id, &rect);
	ERR_FAIL_COND_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to create available area for id: %d, error: %d.", p_display_id, err));
	I->value.available_rect = Rect2i(rect->left, rect->top, rect->width, rect->height);
	OH_NativeDisplayManager_DestroyAvailableArea(rect);
}

void DisplayServerOpenHarmony::fold_display_mode_change_callback(NativeDisplayManager_FoldDisplayMode p_display_mode) {
	get_singleton()->fold_display_mode = (FoldDisplayMode)p_display_mode;
}

TypedArray<Rect2> DisplayServerOpenHarmony::get_display_cutouts(int p_screen) const {
	TypedArray<Rect2> ret;
	NativeDisplayManager_CutoutInfo *cutout_info = nullptr;

	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateDefaultDisplayCutoutInfo(&cutout_info);
	ERR_FAIL_COND_V_MSG(err != DISPLAY_MANAGER_OK, ret, vformat("Failed to fetch default display cutout info, error: %d.", err));

	for (int i = 0; i < cutout_info->boundingRectsLength; i++) {
		NativeDisplayManager_Rect &rect = cutout_info->boundingRects[i];
		ret.push_back(Rect2(rect.left, rect.top, rect.width, rect.height));
	}

	err = OH_NativeDisplayManager_DestroyDefaultDisplayCutoutInfo(cutout_info);
	ERR_FAIL_COND_V_MSG(err != DISPLAY_MANAGER_OK, ret, vformat("Failed to destroy default display cutout info, error: %d.", err));
	return ret;
}

int DisplayServerOpenHarmony::get_screen_count() const {
	return screen_ids.size();
}

int DisplayServerOpenHarmony::get_primary_screen() const {
	NativeDisplayManager_DisplayInfo *display_info = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreatePrimaryDisplay(&display_info);
	ERR_FAIL_COND_V_MSG(err != DISPLAY_MANAGER_OK, DisplayServerEnums::INVALID_SCREEN, vformat("Failed to fetch the primary display, error: %d.", err));
	uint64_t display_id = display_info->id;
	OH_NativeDisplayManager_DestroyDisplay(display_info);

	int idx = 0;
	bool found = false;
	for (; idx < screen_ids.size(); idx++) {
		if (display_id == screen_ids[idx]) {
			found = true;
			break;
		}
	}
	ERR_FAIL_COND_V_MSG(!found, DisplayServerEnums::INVALID_SCREEN, vformat("The primary display %d wasn't cached.", display_id));
	return idx;
}

int DisplayServerOpenHarmony::get_screen_from_rect(const Rect2 &p_rect) const {
	real_t best_score = 0.0;
	int best_screen = DisplayServerEnums::INVALID_SCREEN;

	for (int i = 0; i < screen_ids.size(); i++) {
		uint64_t display_id = screen_ids[i];
		HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
		ERR_CONTINUE_MSG(!I, vformat("The screen %d (display id %d) wasn't cached.", i, display_id));

		Rect2 global_rect = Rect2(I->value.global_position, I->value.size);
		real_t score = global_rect.intersection(p_rect).get_area();
		if (score > best_score) {
			best_score = score;
			best_screen = i;
		}
	}

	return best_screen;
}

Point2i DisplayServerOpenHarmony::screen_get_position(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Point2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Point2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.global_position;
}

Size2i DisplayServerOpenHarmony::screen_get_size(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Size2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Size2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.size;
}

Rect2i DisplayServerOpenHarmony::screen_get_usable_rect(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Rect2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Rect2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.available_rect;
}

int DisplayServerOpenHarmony::screen_get_dpi(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), 0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, 0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.density_dpi;
}

float DisplayServerOpenHarmony::screen_get_scale(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), -1.0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, -1.0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.scaled_density;
}

float DisplayServerOpenHarmony::screen_get_refresh_rate(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), -1.0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, -1.0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.refresh_rate;
}

Color DisplayServerOpenHarmony::screen_get_pixel(const Point2i &p_position) const {
	bool granted = OS::get_singleton()->request_permission("ohos.permission.CUSTOM_SCREEN_CAPTURE");
	ERR_FAIL_COND_V_MSG(!granted, Color(), "ohos.permission.CUSTOM_SCREEN_CAPTURE permission not granted.");

	int screen_id = DisplayServerEnums::INVALID_SCREEN;
	uint32_t display_id = 0;
	Point2i local_pos;
	for (int i = 0; i < screen_ids.size(); i++) {
		uint64_t id = screen_ids[i];
		HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(id);
		ERR_CONTINUE_MSG(!I, vformat("The screen %d (display id %d) wasn't cached.", i, id));

		if (I->value.global_position <= p_position && p_position < (I->value.global_position + I->value.size)) {
			screen_id = i;
			display_id = id;
			local_pos = p_position - I->value.global_position;
			break;
		}
	}
	ERR_FAIL_COND_V(screen_id == DisplayServerEnums::INVALID_SCREEN, Color());

	OH_PixelmapNative *screenshot = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CaptureScreenPixelmap(display_id, &screenshot);
	ERR_FAIL_COND_V_MSG(err != DISPLAY_MANAGER_OK, Color(), vformat("Failed to capture screen %d (display id: %d), error: %d.", screen_id, display_id, err));

	Error error;
	Color result = PixelmapDriver::pixelmap_get_color(screenshot, p_position, error);
	OH_PixelmapNative_Destroy(&screenshot);
	ERR_FAIL_COND_V_MSG(error != OK, Color(), vformat("Failed to get pixel from position %v in screen %d (display id: %d), error: %d.", p_position, screen_id, display_id, error));

	return result;
}

Ref<Image> DisplayServerOpenHarmony::screen_get_image(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	bool granted = OS::get_singleton()->request_permission("ohos.permission.CUSTOM_SCREEN_CAPTURE");
	ERR_FAIL_COND_V_MSG(!granted, Ref<Image>(), "ohos.permission.CUSTOM_SCREEN_CAPTURE permission not granted.");
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Ref<Image>());

	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Ref<Image>(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));

	OH_PixelmapNative *screenshot = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CaptureScreenPixelmap(display_id, &screenshot);
	ERR_FAIL_COND_V_MSG(err != DISPLAY_MANAGER_OK, Ref<Image>(), vformat("Failed to capture screen %d (display id: %d), error: %d.", p_screen, display_id, err));

	Ref<Image> result;
	Error error = PixelmapDriver::pixelmap_to_image(screenshot, result);
	OH_PixelmapNative_Destroy(&screenshot);
	screenshot = nullptr;
	ERR_FAIL_COND_V_MSG(error != OK, Ref<Image>(), vformat("Failed to capture screen %d (display id: %d), error: %d.", p_screen, display_id, error));

	return result;
}

Ref<Image> DisplayServerOpenHarmony::screen_get_image_rect(const Rect2i &p_rect) const {
	bool granted = OS::get_singleton()->request_permission("ohos.permission.CUSTOM_SCREEN_CAPTURE");
	ERR_FAIL_COND_V_MSG(!granted, Ref<Image>(), "ohos.permission.CUSTOM_SCREEN_CAPTURE permission not granted.");

	Ref<Image> result;

	for (int i = 0; i < screen_ids.size(); i++) {
		uint64_t display_id = screen_ids[i];
		HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
		ERR_CONTINUE_MSG(!I, vformat("The screen %d (display id %d) wasn't cached.", i, display_id));

		const Rect2i screen_rect = Rect2i(I->value.global_position, I->value.size);
		const Rect2i intersect = p_rect.intersection(screen_rect);
		if (!intersect.has_area()) {
			continue;
		}

		OH_PixelmapNative *screenshot = nullptr;
		NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CaptureScreenPixelmap(display_id, &screenshot);
		ERR_CONTINUE_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to capture rect %s in screen %d (display id: %d), error: %d.", intersect, i, display_id, err));

		Ref<Image> partial;
		Error error = PixelmapDriver::pixelmap_get_image_rect(screenshot, Rect2i(intersect.position - screen_rect.position, intersect.size), partial);
		OH_PixelmapNative_Destroy(&screenshot);
		ERR_CONTINUE(error != OK);

		const Point2i dest_pos = intersect.position - p_rect.position;

		if (result.is_null()) {
			result = Image::create_empty(p_rect.size.width, p_rect.size.height, false, Image::FORMAT_RGBA8);
		}
		result->blit_rect(partial, Rect2i(Point2i(), intersect.size), dest_pos);
	}

	return result;
}

bool DisplayServerOpenHarmony::is_touchscreen_available() const {
	return true;
}

void DisplayServerOpenHarmony::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	p_screen = _get_screen_index(p_screen);
	// screen_manager->screen_set_orientation(p_orientation, p_screen);
}

DisplayServerEnums::ScreenOrientation DisplayServerOpenHarmony::screen_get_orientation(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), DisplayServerEnums::SCREEN_PORTRAIT);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, DisplayServerEnums::SCREEN_PORTRAIT, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.orientation;
}

void DisplayServerOpenHarmony::screen_set_keep_on(bool p_enable) {
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		ohos_wrapper_screen_set_keep_on(E.value.native_window_id, p_enable);
	}
}

bool DisplayServerOpenHarmony::screen_is_kept_on() const {
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		ohos_wrapper_screen_is_kept_on(E.value.native_window_id);
	}
	return false;
}

void DisplayServerOpenHarmony::_mouse_update_mode() {
}

void DisplayServerOpenHarmony::mouse_set_mode(DisplayServerEnums::MouseMode p_mode) {
	ERR_FAIL_INDEX(p_mode, DisplayServerEnums::MouseMode::MOUSE_MODE_MAX);
	if (p_mode == mouse_mode_base) {
		return;
	}
	mouse_mode_base = p_mode;
	_mouse_update_mode();
}

DisplayServerEnums::MouseMode DisplayServerOpenHarmony::mouse_get_mode() const {
	return mouse_mode_base;
}

void DisplayServerOpenHarmony::mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) {
	ERR_FAIL_INDEX(p_mode, DisplayServerEnums::MouseMode::MOUSE_MODE_MAX);
	if (p_mode == mouse_mode_override) {
		return;
	}
	mouse_mode_override = p_mode;
	_mouse_update_mode();
}

DisplayServerEnums::MouseMode DisplayServerOpenHarmony::mouse_get_mode_override() const {
	return mouse_mode_override;
}

void DisplayServerOpenHarmony::mouse_set_mode_override_enabled(bool p_override_enabled) {
	if (p_override_enabled == mouse_mode_override_enabled) {
		return;
	}
	mouse_mode_override_enabled = p_override_enabled;
	_mouse_update_mode();
}

bool DisplayServerOpenHarmony::mouse_is_mode_override_enabled() const {
	return mouse_mode_override_enabled;
}

void DisplayServerOpenHarmony::warp_mouse(const Point2i &p_position) {
}

Point2i DisplayServerOpenHarmony::mouse_get_position() const {
	return Input::get_singleton()->get_mouse_position();
}

BitField<MouseButtonMask> DisplayServerOpenHarmony::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

void DisplayServerOpenHarmony::_cursor_update_shape(bool p_update_custom) {
	HashMap<DisplayServerEnums::CursorShape, CustomCursor>::ConstIterator I = custom_cursors.find(cursor_shape);
	if (!I) {
		for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
			OH_Input_SetPointerStyle(E.value.native_window_id, cursors[cursor_shape]);
		}
		return;
	}

	if (!p_update_custom && last_cursor_shape == cursor_shape && last_cursor_shape != DisplayServerEnums::CURSOR_MAX) {
		for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
			OH_Input_SetPointerStyle(E.value.native_window_id, DEVELOPER_DEFINED_ICON);
		}
		return;
	}

	Input_CursorConfig *cursor_config = OH_Input_CursorConfig_Create(false);
	ERR_FAIL_NULL(cursor_config);

	OH_PixelmapNative *pixelmap = nullptr;
	Error err = PixelmapDriver::image_to_pixelmap(I->value.image, pixelmap);
	if (err != OK) {
		if (pixelmap) {
			OH_PixelmapNative_Destroy(&pixelmap);
		}
		OH_Input_CursorConfig_Destroy(&cursor_config);
		ERR_PRINT(vformat("Failed to convert image to cursor pixelmap for shape %d with error: %d.", cursor_shape, err));
		return;
	}

	Input_CustomCursor *cursor = OH_Input_CustomCursor_Create(pixelmap, I->value.hotspot.x, I->value.hotspot.y);
	if (!cursor) {
		OH_PixelmapNative_Destroy(&pixelmap);
		OH_Input_CursorConfig_Destroy(&cursor_config);
		ERR_PRINT(vformat("Failed to create custom cursor for shape %d.", cursor_shape));
	}

	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		Input_Result error = OH_Input_SetCustomCursor(E.value.native_window_id, cursor, cursor_config);
		if (error != INPUT_SUCCESS) {
			OH_Input_CustomCursor_Destroy(&cursor);
			OH_PixelmapNative_Destroy(&pixelmap);
			OH_Input_CursorConfig_Destroy(&cursor_config);
			ERR_PRINT(vformat("Failed to set custom cursor for shape %d with Input_Result: %d.", cursor_shape, error));
			return;
		}
		OH_Input_SetPointerStyle(E.value.native_window_id, DEVELOPER_DEFINED_ICON);
	}
	OH_Input_CursorConfig_Destroy(&cursor_config);

	if (custom_cursor) {
		pixelmap = nullptr;
		OH_Input_CustomCursor_GetPixelMap(custom_cursor, &pixelmap);
		OH_Input_CustomCursor_Destroy(&custom_cursor);
		OH_PixelmapNative_Destroy(&pixelmap);
	}

	last_cursor_shape = cursor_shape;
	custom_cursor = cursor;
}

void DisplayServerOpenHarmony::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	ERR_FAIL_INDEX(p_shape, DisplayServerEnums::CURSOR_MAX);
	if (cursor_shape == p_shape) {
		return;
	}
	cursor_shape = p_shape;

	_cursor_update_shape();
}

DisplayServerEnums::CursorShape DisplayServerOpenHarmony::cursor_get_shape() const {
	return cursor_shape;
}

void DisplayServerOpenHarmony::cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
	ERR_FAIL_INDEX(p_shape, DisplayServerEnums::CURSOR_MAX);
	if (p_cursor.is_null()) {
		custom_cursors.erase(p_shape);
		cursor_shape = p_shape;
		_cursor_update_shape();
		return;
	}

	CustomCursor &custom = custom_cursors[p_shape];
	if (custom.image == p_cursor && cursor_shape == p_shape && custom.hotspot == p_hotspot) {
		return;
	}

	custom.image = p_cursor;
	custom.hotspot = p_hotspot;

	_cursor_update_shape(true);
}

Point2i DisplayServerOpenHarmony::ime_get_selection() const {
	return config.im_selection;
}

String DisplayServerOpenHarmony::ime_get_text() const {
	return im_text;
}

void DisplayServerOpenHarmony::_input_text_key(Key p_key, char32_t p_char, bool p_pressed, bool p_ctrl_pressed) {
	Ref<InputEventKey> iev;
	iev.instantiate();
	iev->set_echo(false);
	iev->set_pressed(p_pressed);
	iev->set_keycode(fix_keycode(p_char, p_key));
	iev->set_key_label(fix_key_label(p_char, p_key));
	// iev->set_physical_keycode(p_physical);
	iev->set_unicode(fix_unicode(p_char));
	iev->set_ctrl_pressed(p_ctrl_pressed);
	Input::get_singleton()->parse_input_event(iev);
}

void DisplayServerOpenHarmony::_clear_text_preview() {
	if (config.im_selection.y == 0) {
		return;
	}
	im_text = String();
	config.im_selection.y = 0;
	callable_mp((Object *)OS_OpenHarmony::get_singleton()->get_main_loop(), &Object::notification).call_deferred(MainLoop::NOTIFICATION_OS_IME_UPDATE, false);
}

void DisplayServerOpenHarmony::_get_text_config(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_TextConfig *p_text_config) {
	InputMethod_TextInputType input_type = IME_TEXT_INPUT_TYPE_TEXT;
	InputMethod_EnterKeyType enter_key_type = IME_ENTER_KEY_DONE;
	switch (get_singleton()->config.keyboard_type) {
		case DisplayServerEnums::KEYBOARD_TYPE_DEFAULT: {
			input_type = IME_TEXT_INPUT_TYPE_TEXT;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_MULTILINE: {
			input_type = IME_TEXT_INPUT_TYPE_MULTILINE;
			enter_key_type = IME_ENTER_KEY_NEWLINE;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER: {
			input_type = IME_TEXT_INPUT_TYPE_NUMBER;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL: {
			input_type = IME_TEXT_INPUT_TYPE_NUMBER_DECIMAL;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_PHONE: {
			input_type = IME_TEXT_INPUT_TYPE_PHONE;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_EMAIL_ADDRESS: {
			input_type = IME_TEXT_INPUT_TYPE_EMAIL_ADDRESS;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_PASSWORD: {
			input_type = IME_TEXT_INPUT_TYPE_VISIBLE_PASSWORD;
		} break;
		case DisplayServerEnums::KEYBOARD_TYPE_URL: {
			input_type = IME_TEXT_INPUT_TYPE_URL;
		} break;
		default: {
		} break;
	}
	ERR_PRINT(vformat("_get_text_config,input_type:%d,enter_key_type:%d.", input_type, enter_key_type));
	OH_TextConfig_SetInputType(p_text_config, input_type);
	OH_TextConfig_SetPreviewTextSupport(p_text_config, true);
	OH_TextConfig_SetEnterKeyType(p_text_config, enter_key_type);
}

void DisplayServerOpenHarmony::_insert_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length) {
	const String chars = String::utf16(p_text, p_length);
	ERR_PRINT(vformat("insert text:%s,length:%d.", chars, p_length));
	get_singleton()->_clear_text_preview();
	get_singleton()->config.im_selection.x += p_length;
	for (int i = 0; i < chars.length(); i++) {
		char32_t character = chars[i];
		Key key = Key::NONE;

		if (character == '\t') { // 0x09
			key = Key::TAB;
		} else if (character == '\n') { // 0x0A
			key = Key::ENTER;
		} else if (character == 0x2006) {
			key = Key::SPACE;
		}

		_input_text_key(key, character, true);
		_input_text_key(key, character, false);
	}
}

void DisplayServerOpenHarmony::_delete_forward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	get_singleton()->_clear_text_preview();
	for (int32_t i = 0; i < p_length; i++) {
		_input_text_key(Key::KEY_DELETE, 0, true);
		_input_text_key(Key::KEY_DELETE, 0, false);
	}
}

void DisplayServerOpenHarmony::_delete_backward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	get_singleton()->_clear_text_preview();
	get_singleton()->config.im_selection.x = MAX(0, get_singleton()->config.im_selection.x - p_length);
	for (int32_t i = 0; i < p_length; i++) {
		_input_text_key(Key::BACKSPACE, 0, true);
		_input_text_key(Key::BACKSPACE, 0, false);
	}
}

void DisplayServerOpenHarmony::_send_keyboard_status(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_KeyboardStatus p_status) {
	ERR_PRINT(vformat("_send_keyboard_status,type:%d.", p_status));
	get_singleton()->keyboard_visible = p_status == IME_KEYBOARD_STATUS_SHOW;
}

void DisplayServerOpenHarmony::_send_enter_key(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_EnterKeyType p_enter_key_type) {
	ERR_PRINT(vformat("_send_enter_key,type:%d.", p_enter_key_type));
	get_singleton()->_clear_text_preview();
	_input_text_key(Key::ENTER, 0, true);
	_input_text_key(Key::ENTER, 0, false);
}

void DisplayServerOpenHarmony::_move_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_Direction p_direction) {
	ERR_PRINT(vformat("_move_cursor,direction:%d.", p_direction));
	switch (p_direction) {
		case IME_DIRECTION_UP: {
			_input_text_key(Key::UP, 0, true);
			_input_text_key(Key::UP, 0, false);
		} break;
		case IME_DIRECTION_DOWN: {
			_input_text_key(Key::DOWN, 0, true);
			_input_text_key(Key::DOWN, 0, false);
		} break;
		case IME_DIRECTION_LEFT: {
			_input_text_key(Key::LEFT, 0, true);
			_input_text_key(Key::LEFT, 0, false);
		} break;
		case IME_DIRECTION_RIGHT: {
			_input_text_key(Key::RIGHT, 0, true);
			_input_text_key(Key::RIGHT, 0, false);
		} break;
		default: {
		} break;
	}
}

void DisplayServerOpenHarmony::_handle_set_selection(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_start, int32_t p_end) {
	ERR_PRINT(vformat("_handle_set_selection,start:%d,end:%d.", p_start, p_end));
}

void DisplayServerOpenHarmony::_handle_extend_action(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_ExtendAction p_action) {
	ERR_PRINT(vformat("_handle_extend_action, p_action:%d.", p_action));
	switch (p_action) {
		case IME_EXTEND_ACTION_SELECT_ALL: {
			_input_text_key(Key::A, 0, true, true);
			_input_text_key(Key::A, 0, false, true);
		} break;
		case IME_EXTEND_ACTION_CUT: {
			_input_text_key(Key::X, 0, true, true);
			_input_text_key(Key::X, 0, false, true);
		} break;
		case IME_EXTEND_ACTION_COPY: {
			_input_text_key(Key::C, 0, true, true);
			_input_text_key(Key::C, 0, false, true);
		} break;
		case IME_EXTEND_ACTION_PASTE: {
			_input_text_key(Key::V, 0, true, true);
			_input_text_key(Key::V, 0, false, true);
		} break;
		default: {
		} break;
	}
}

void DisplayServerOpenHarmony::_get_left_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	ERR_PRINT(vformat("_get_left_text_of_cursor,im_text:%s,im_selection:%v, length:%d, number:%d,text:%s.", get_singleton()->im_text, get_singleton()->config.im_selection, *p_length, p_number, String::utf16(p_text, *p_length)));
}

void DisplayServerOpenHarmony::_get_right_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	ERR_PRINT(vformat("_get_right_text_of_cursor=======,im_text:%s,im_selection:%v, length:%d, number:%d,text:%s.", get_singleton()->im_text, get_singleton()->config.im_selection, *p_length, p_number, String::utf16(p_text, *p_length)));
}

int32_t DisplayServerOpenHarmony::_get_text_index_at_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	return 0;
}

int32_t DisplayServerOpenHarmony::_receive_private_command(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_PrivateCommand *p_command[], size_t p_length) {
	return 0;
}

int32_t DisplayServerOpenHarmony::_set_preview_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length, int32_t p_start, int32_t p_end) {
	if (!get_singleton()->ime_active) {
		return 0;
	}
	get_singleton()->im_text = String::utf16(p_text, p_length);
	if (p_start >= 0) {
		get_singleton()->config.im_selection.x = p_start;
	}
	get_singleton()->config.im_selection.y = p_length;
	callable_mp((Object *)OS_OpenHarmony::get_singleton()->get_main_loop(), &Object::notification).call_deferred(MainLoop::NOTIFICATION_OS_IME_UPDATE, false);
	return 0;
}

void DisplayServerOpenHarmony::_finish_text_preview(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	get_singleton()->_clear_text_preview();
}

void DisplayServerOpenHarmony::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (keyboard_visible && config.keyboard_type == p_type) {
		return;
	}

	if (keyboard_visible) {
		virtual_keyboard_hide();
	}

	config.im_selection.x = p_cursor_start;
	config.im_selection.y = MAX(p_cursor_end - p_cursor_start, 0);

	ERR_PRINT(vformat("text:%s,position:%v,size:%v,max length:%d,cursor start:%d,cursor end:%d", p_existing_text, p_screen_rect.position, p_screen_rect.size, p_max_length, p_cursor_start, p_cursor_end));

	config.keyboard_type = p_type;
	text_editor_proxy = OH_TextEditorProxy_Create();
	attach_options = OH_AttachOptions_Create(true);

	OH_TextEditorProxy_SetGetTextConfigFunc(text_editor_proxy, _get_text_config);
	OH_TextEditorProxy_SetInsertTextFunc(text_editor_proxy, _insert_text);
	OH_TextEditorProxy_SetDeleteForwardFunc(text_editor_proxy, _delete_forward);
	OH_TextEditorProxy_SetDeleteBackwardFunc(text_editor_proxy, _delete_backward);
	OH_TextEditorProxy_SetSendKeyboardStatusFunc(text_editor_proxy, _send_keyboard_status);
	OH_TextEditorProxy_SetSendEnterKeyFunc(text_editor_proxy, _send_enter_key);
	OH_TextEditorProxy_SetMoveCursorFunc(text_editor_proxy, _move_cursor);
	OH_TextEditorProxy_SetHandleSetSelectionFunc(text_editor_proxy, _handle_set_selection);
	OH_TextEditorProxy_SetHandleExtendActionFunc(text_editor_proxy, _handle_extend_action);
	OH_TextEditorProxy_SetGetLeftTextOfCursorFunc(text_editor_proxy, _get_left_text_of_cursor);
	OH_TextEditorProxy_SetGetRightTextOfCursorFunc(text_editor_proxy, _get_right_text_of_cursor);
	OH_TextEditorProxy_SetGetTextIndexAtCursorFunc(text_editor_proxy, _get_text_index_at_cursor);
	OH_TextEditorProxy_SetReceivePrivateCommandFunc(text_editor_proxy, _receive_private_command);
	OH_TextEditorProxy_SetSetPreviewTextFunc(text_editor_proxy, _set_preview_text);
	OH_TextEditorProxy_SetFinishTextPreviewFunc(text_editor_proxy, _finish_text_preview);

	InputMethod_ErrorCode code = OH_InputMethodController_Attach(text_editor_proxy, attach_options, &input_method_proxy);

	if (code != IME_ERR_OK) {
		OH_TextEditorProxy_Destroy(text_editor_proxy);
		text_editor_proxy = nullptr;
		OH_AttachOptions_Destroy(attach_options);
		attach_options = nullptr;
		ERR_FAIL_MSG(vformat("Failed to attach input method controller: %d.", code));
	}
}

void DisplayServerOpenHarmony::virtual_keyboard_hide() {
	if (keyboard_visible) {
		if (OH_InputMethodProxy_HideKeyboard(input_method_proxy) != IME_ERR_OK) {
			ERR_PRINT("Failed to hide keyboard.");
		}
		keyboard_visible = false;
	}
	if (input_method_proxy) {
		if (OH_InputMethodController_Detach(input_method_proxy) != IME_ERR_OK) {
			ERR_PRINT("Failed to detach input method controller.");
		}
		input_method_proxy = nullptr;
	}
	if (attach_options) {
		OH_AttachOptions_Destroy(attach_options);
		attach_options = nullptr;
	}
	if (text_editor_proxy) {
		OH_TextEditorProxy_Destroy(text_editor_proxy);
		text_editor_proxy = nullptr;
	}
	_clear_text_preview();
}

int DisplayServerOpenHarmony::virtual_keyboard_get_height() const {
	if (keyboard_visible) {
		int height = ohos_wrapper_get_keyboard_avoid_area(main_native_window_id);
		return height;
	}
	return 0;
}

void DisplayServerOpenHarmony::window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window) {
	if (ime_active == p_active) {
		return;
	}
	ime_active = p_active;
	if (!ime_active && keyboard_visible) {
		virtual_keyboard_hide();
	}
}

void DisplayServerOpenHarmony::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	if (ime_active && keyboard_visible) {
		InputMethod_CursorInfo *info = OH_CursorInfo_Create(p_pos.x, p_pos.y, 0, 30);
		OH_InputMethodProxy_NotifyCursorUpdate(input_method_proxy, info);
		OH_CursorInfo_Destroy(info);
	}
}

void DisplayServerOpenHarmony::clipboard_set(const String &p_text) {
	OH_UdsPlainText *text = OH_UdsPlainText_Create();
	OH_UdsPlainText_SetContent(text, p_text.utf8().get_data());
	OH_UdmfRecord *record = OH_UdmfRecord_Create();
	OH_UdmfRecord_AddPlainText(record, text);
	OH_UdmfData *data = OH_UdmfData_Create();
	OH_UdmfData_AddRecord(data, record);
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	OH_Pasteboard_SetData(pasteboard, data);
	OH_Pasteboard_Destroy(pasteboard);
	OH_UdmfData_Destroy(data);
	OH_UdmfRecord_Destroy(record);
	OH_UdsPlainText_Destroy(text);
}

String DisplayServerOpenHarmony::clipboard_get() const {
	bool granted = OS::get_singleton()->request_permission("ohos.permission.READ_PASTEBOARD");
	ERR_FAIL_COND_V_MSG(!granted, String(), "ohos.permission.READ_PASTEBOARD permission not granted.");

	String content;
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	bool has_text = OH_Pasteboard_HasType(pasteboard, PASTEBOARD_MIMETYPE_TEXT_PLAIN);
	if (has_text) {
		int status = 0;
		OH_UdmfData *udmf_data = OH_Pasteboard_GetData(pasteboard, &status);
		if (status == 0) { // PASTEBOARD_ErrCode::ERR_OK.
			OH_UdmfRecord *record = OH_UdmfData_GetRecord(udmf_data, 0);
			OH_UdsPlainText *text = OH_UdsPlainText_Create();
			OH_UdmfRecord_GetPlainText(record, text);
			content = String::utf8(OH_UdsPlainText_GetContent(text));
			OH_UdsPlainText_Destroy(text);
			OH_UdmfRecord_Destroy(record);
			OH_UdmfData_Destroy(udmf_data);
		} else {
			ERR_PRINT(vformat("Failed to get clipboard data with PASTEBOARD_ErrCode: %d.", status));
		}
	}
	OH_Pasteboard_Destroy(pasteboard);
	return content;
}

Ref<Image> DisplayServerOpenHarmony::clipboard_get_image() const {
	bool granted = OS::get_singleton()->request_permission("ohos.permission.READ_PASTEBOARD");
	ERR_FAIL_COND_V_MSG(!granted, Ref<Image>(), "ohos.permission.READ_PASTEBOARD permission not granted.");

	Ref<Image> content;
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	bool has_text = OH_Pasteboard_HasType(pasteboard, PASTEBOARD_MIMETYPE_PIXELMAP);
	if (has_text) {
		int status = 0;
		OH_UdmfData *udmf_data = OH_Pasteboard_GetData(pasteboard, &status);
		if (status == 0) { // PASTEBOARD_ErrCode::ERR_OK.
			OH_UdmfRecord *record = OH_UdmfData_GetRecord(udmf_data, 0);
			OH_UdsPixelMap *pixelmap = OH_UdsPixelMap_Create();
			OH_UdmfRecord_GetPixelMap(record, pixelmap);
			OH_PixelmapNative *pixelmap_native = nullptr;
			OH_UdsPixelMap_GetPixelMap(pixelmap, pixelmap_native);
			Error err = PixelmapDriver::pixelmap_to_image(pixelmap_native, content);
			if (err != OK) {
				ERR_PRINT(vformat("Failed to get image with error: %d.", err));
			}
			OH_UdsPixelMap_Destroy(pixelmap);
			OH_UdmfRecord_Destroy(record);
			OH_UdmfData_Destroy(udmf_data);
		} else {
			ERR_PRINT(vformat("Failed to get clipboard data with PASTEBOARD_ErrCode: %d.", status));
		}
	}
	OH_Pasteboard_Destroy(pasteboard);
	return content;
}

bool DisplayServerOpenHarmony::_clipboard_has(const char *p_type) const {
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	bool has_type = OH_Pasteboard_HasType(pasteboard, p_type);
	OH_Pasteboard_Destroy(pasteboard);
	return has_type;
}

bool DisplayServerOpenHarmony::clipboard_has() const {
	return _clipboard_has(PASTEBOARD_MIMETYPE_TEXT_PLAIN);
}

bool DisplayServerOpenHarmony::clipboard_has_image() const {
	return _clipboard_has(PASTEBOARD_MIMETYPE_PIXELMAP);
}

void DisplayServerOpenHarmony::set_context(DisplayServerEnums::Context p_context) {
	context = p_context;

	// for (KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
	// 	_update_context(E.value);
	// }
}

bool DisplayServerOpenHarmony::get_swap_cancel_ok() {
	return swap_cancel_ok;
}

Vector<DisplayServerEnums::WindowID> DisplayServerOpenHarmony::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> ret;
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		ret.push_back(E.key);
	}
	return ret;
}

DisplayServerEnums::WindowID DisplayServerOpenHarmony::create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect, bool p_exclusive, DisplayServerEnums::WindowID p_transient_parent) {
	DisplayServerEnums::WindowID id = window_id_counter;
	WindowData &wd = windows[id];

	wd.id = id;
	wd.mode = p_mode;
	wd.flags = p_flags;
	wd.vsync_mode = p_vsync_mode;

	// NOTE: Remember to clear its position if this window will be a toplevel. We
	// can only know once we show it.
	wd.rect = p_rect;

	wd.title = "Godot";
	wd.parent_id = p_transient_parent;
	return id;
}

void DisplayServerOpenHarmony::show_window(DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	WindowData &wd = windows[p_window];

	if (!wd.created) {
		DisplayServerEnums::WindowMode setup_mode = wd.mode;

		DisplayServerEnums::WindowID root_id = wd.id;
		while (root_id != DisplayServerEnums::INVALID_WINDOW_ID && window_get_flag(DisplayServerEnums::WINDOW_FLAG_POPUP_WM_HINT, root_id)) {
			root_id = windows[root_id].parent_id;
		}
		ERR_FAIL_COND(root_id == DisplayServerEnums::INVALID_WINDOW_ID);

		wd.root_id = root_id;

		if (!window_get_flag(DisplayServerEnums::WINDOW_FLAG_POPUP_WM_HINT, p_window)) {
			// NOTE: DO **NOT** KEEP THE POSITION SET FOR TOPLEVELS. Wayland does not
			// track them and we're gonna get our events transformed in unexpected ways.
			wd.rect.position = Point2i();

			// Since it can't have a position. Let's tell the window node the news by
			// sending the actual rect to it.
			if (wd.rect_changed_callback.is_valid()) {
				wd.rect_changed_callback.call(wd.rect);
			}
		} else {
			windows[root_id].popup_stack.push_back(p_window);

			if (window_get_flag(DisplayServerEnums::WINDOW_FLAG_POPUP, p_window)) {
				// Reroutes all input to it.
			}
		}

		wd.created = true;

		// NOTE: The XDG shell protocol is built in a way that causes the window to
		// be immediately shown as soon as a valid buffer is assigned to it. Hence,
		// the only acceptable way of implementing window showing is to move the
		// graphics context window creation logic here.
#ifdef RD_ENABLED
		if (rendering_context) {
			union {
#ifdef VULKAN_ENABLED
				RenderingContextDriverVulkanOpenHarmony::WindowPlatformData vulkan;
#endif // VULKAN_ENABLED
			} wpd;
#ifdef VULKAN_ENABLED
			if (rendering_driver == "vulkan") {
				wpd.vulkan.window = wd.native_window;
			}
#endif // VULKAN_ENABLED
			Error err = rendering_context->window_create(wd.id, &wpd);
			ERR_FAIL_COND_MSG(err != OK, vformat("Can't create a %s window", rendering_driver));

			rendering_context->window_set_size(wd.id, wd.rect.size.width, wd.rect.size.height);

			window_set_vsync_mode(wd.vsync_mode, p_window);

			if (rendering_device) {
				rendering_device->screen_create(wd.id);
			}
		}
#endif // RD_ENABLED
		wd.visible = true;

		// Actually try to apply the window's mode now that it's visible.
		window_set_mode(setup_mode, wd.id);
	}
}

void DisplayServerOpenHarmony::delete_sub_window(DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(p_window == DisplayServerEnums::MAIN_WINDOW_ID, "Main window can't be deleted");
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	WindowData &wd = windows[p_window];

	ERR_FAIL_COND(!windows.has(wd.root_id));
	WindowData &root_wd = windows[wd.root_id];

	if (root_wd.popup_stack.back() && root_wd.popup_stack.back()->get() == p_window) {
		root_wd.popup_stack.pop_back();
	}
#ifdef RD_ENABLED
	if (rendering_device) {
		rendering_device->screen_free(p_window);
	}

	if (rendering_context) {
		rendering_context->window_destroy(p_window);
	}
#endif // RD_ENABLED

	windows.erase(p_window);
}

DisplayServerEnums::WindowID DisplayServerOpenHarmony::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

void DisplayServerOpenHarmony::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id = p_instance;
}

ObjectID DisplayServerOpenHarmony::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id;
}

void DisplayServerOpenHarmony::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	windows[p_window].window_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	windows[p_window].input_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	windows[p_window].input_text_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	windows[p_window].rect_changed_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));

	windows[p_window].drop_files_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

int DisplayServerOpenHarmony::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(p_window != DisplayServerEnums::MAIN_WINDOW_ID, DisplayServerEnums::INVALID_SCREEN, vformat("The window %d isn't main.", p_window));
	return 0;
}

void DisplayServerOpenHarmony::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Point2i DisplayServerOpenHarmony::window_get_position(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerOpenHarmony::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Point2i();
}

void DisplayServerOpenHarmony::window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_max_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOpenHarmony::window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_min_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i();
}

void DisplayServerOpenHarmony::window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));
	WindowData &wd = windows[p_window];

	if (wd.rect.size == p_size) {
		return;
	}

	Size2i new_size = p_size;
	new_size = p_size.maxi(1);

	_update_window_rect(Rect2i(wd.rect.position, new_size), p_window);
}

Size2i DisplayServerOpenHarmony::window_get_size(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), Size2i(), vformat("The window %d does exist.", p_window));
	return windows[p_window].rect.size;
}

Size2i DisplayServerOpenHarmony::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), Size2i(), vformat("The window %d does exist.", p_window));
	return windows[p_window].rect.size;
}

void DisplayServerOpenHarmony::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::WindowMode DisplayServerOpenHarmony::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WINDOW_MODE_FULLSCREEN;
}

void DisplayServerOpenHarmony::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));
#ifdef VULKAN_ENABLED
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif // VULKAN_ENABLED
}

DisplayServerEnums::VSyncMode DisplayServerOpenHarmony::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
#ifdef VULKAN_ENABLED
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
#endif // VULKAN_ENABLED
	return DisplayServerEnums::VSYNC_ENABLED;
}

bool DisplayServerOpenHarmony::window_is_hdr_output_supported(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), false, vformat("The window %d does exist.", p_window));
	bool renderer_supports_hdr_output = false;
	bool surface_supports_hdr_output = false;
#if defined(RD_ENABLED)
	if (rendering_device && rendering_device->has_feature(RenderingDevice::Features::SUPPORTS_HDR_OUTPUT)) {
		renderer_supports_hdr_output = true;
		surface_supports_hdr_output = rendering_device->screen_get_hdr_output_supported(p_window);
	}
#endif
	if (!renderer_supports_hdr_output) {
		return false;
	}

	if (!surface_supports_hdr_output) {
		return false;
	}

	// const WindowData &wd = windows[p_window];

	// return wd.color_profile.target_max_luminance > wd.color_profile.reference_luminance;
	return true;
}

void DisplayServerOpenHarmony::window_request_hdr_output(const bool p_enabled, DisplayServerEnums::WindowID p_window) {
	if (p_enabled) {
		bool renderer_supports_hdr_output = false;
		bool surface_supports_hdr_output = false;
#if defined(RD_ENABLED)
		if (rendering_device && rendering_device->has_feature(RenderingDevice::Features::SUPPORTS_HDR_OUTPUT)) {
			renderer_supports_hdr_output = true;
			surface_supports_hdr_output = rendering_device->screen_get_hdr_output_supported(p_window);
		}
#endif
		if (!renderer_supports_hdr_output) {
			WARN_PRINT("HDR output requested, but is not supported by the renderer or rendering device driver.");
			return;
		}

		if (!surface_supports_hdr_output) {
			WARN_PRINT("HDR output requested, but the window does not support an HDR format.");
			return;
		}
	}

	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));
	WindowData &wd = windows[p_window];
	wd.hdr_requested = p_enabled;

	// _window_update_hdr_state(wd);
}

bool DisplayServerOpenHarmony::window_is_hdr_output_requested(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), false, vformat("The window %d does exist.", p_window));
	const WindowData &wd = windows[p_window];
	return wd.hdr_requested;
}

bool DisplayServerOpenHarmony::window_is_hdr_output_enabled(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), false, vformat("The window %d does exist.", p_window));
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_hdr_output_enabled(p_window);
	}
#endif
	return false;
}

void DisplayServerOpenHarmony::window_set_hdr_output_reference_luminance(const float p_reference_luminance, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));
	if (p_reference_luminance >= 0.0f) {
		ERR_PRINT_ONCE("Manually setting reference white luminance is not supported on Linux devices, as they provide a user-facing brightness setting that directly controls reference white luminance.");
	}
}

float DisplayServerOpenHarmony::window_get_hdr_output_reference_luminance(DisplayServerEnums::WindowID p_window) const {
	return -1.0;
}

float DisplayServerOpenHarmony::window_get_hdr_output_current_reference_luminance(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), 0.0, vformat("The window %d does exist.", p_window));
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_hdr_output_reference_luminance(p_window);
	}
#endif
	return 0.0f;
}

void DisplayServerOpenHarmony::window_set_hdr_output_max_luminance(const float p_max_luminance, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND_MSG(!windows.has(p_window), vformat("The window %d does exist.", p_window));
	if (p_max_luminance >= 0.0f) {
		ERR_PRINT_ONCE("Manually setting max luminance is not supported on Linux devices as they provide a built-in method of calibrating max luminance without the need for additional apps or tools.");
	}
}

float DisplayServerOpenHarmony::window_get_hdr_output_max_luminance(DisplayServerEnums::WindowID p_window) const {
	return -1.0;
}

float DisplayServerOpenHarmony::window_get_hdr_output_current_max_luminance(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V_MSG(!windows.has(p_window), 0.0, vformat("The window %d does exist.", p_window));
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_hdr_output_max_luminance(p_window);
	}
#endif
	return 0.0f;
}

bool DisplayServerOpenHarmony::window_is_maximize_allowed(DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerOpenHarmony::window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

bool DisplayServerOpenHarmony::window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window) const {
	return false;
}

void DisplayServerOpenHarmony::window_request_attention(DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_move_to_foreground(DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

bool DisplayServerOpenHarmony::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerOpenHarmony::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return true;
}

bool DisplayServerOpenHarmony::can_any_window_draw() const {
	return true;
}

void DisplayServerOpenHarmony::process_events() {
	Input::get_singleton()->flush_buffered_events();
}
