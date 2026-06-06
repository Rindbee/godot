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

#include "input_manager_openharmony.h"
#include "key_mapping_openharmony.h"
#include "os_openharmony.h"
#include "rendering_context_driver_vulkan_openharmony.h"
#include "screen_manager_openharmony.h"
#include "wrapper_openharmony.h"

#include "core/input/input.h"
#include "main/main.h"
#include "servers/display/native_menu.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <arkui/native_key_event.h>
#include <arkui/native_node.h>

void DisplayServerOpenHarmony::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	get_singleton()->send_input_event(p_event);
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
	pos = ScreenManagerOpenharmony::get_singleton()->vp_to_px(pos, _get_screen_index(DisplayServerEnums::SCREEN_OF_MAIN_WINDOW));
	p_event->set_position(pos);
	p_event->set_global_position(pos);

	// Device and window.
	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(p_source_event);
	p_event->set_device(device_id);
	p_event->set_window_id(0);

	// Modifiers.
	_parse_modifiers_from(p_source_event, p_event);
}

void DisplayServerOpenHarmony::_parse_touch_event(ArkUI_UIInputEvent *p_event) {
	Ref<InputEventScreenTouch> ste;
	ste.instantiate();

	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(p_event);
	ste->set_device(device_id);
	// ste->set_window_id(0);

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

	pos = ScreenManagerOpenharmony::get_singleton()->vp_to_px(pos, _get_screen_index(DisplayServerEnums::SCREEN_OF_MAIN_WINDOW));
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

void DisplayServerOpenHarmony::_parse_axis_event(ArkUI_UIInputEvent *p_event) {
	const int32_t source_type = OH_ArkUI_UIInputEvent_GetSourceType(p_event);
	if (source_type == UI_INPUT_EVENT_SOURCE_TYPE_MOUSE) {
		const float angle = OH_ArkUI_AxisEvent_GetVerticalAxisValue(p_event);
		if (Math::is_zero_approx(angle)) {
			return;
		}

		Ref<InputEventMouseButton> mb;
		mb.instantiate();

		if (angle > 0) {
			mb->set_button_index(MouseButton::WHEEL_UP);
		} else {
			mb->set_button_index(MouseButton::WHEEL_DOWN);
		}

		_parse_mouse_event_from(p_event, mb);

		Input::get_singleton()->parse_input_event(mb);
	}
}

void DisplayServerOpenHarmony::_parse_mouse_event(ArkUI_UIInputEvent *p_event) {
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

	BitField<MouseButtonMask> button_mask = MouseButtonMask::NONE;
	if (button_index != MouseButton::NONE) {
		button_mask = mouse_button_to_mask(button_index);
	}
	me->set_button_mask(button_mask);
	_parse_mouse_event_from(p_event, me); // Additional mask.

	Input::get_singleton()->parse_input_event(me);
}

void DisplayServerOpenHarmony::_parse_key_event(ArkUI_UIInputEvent *p_event) {
	const ArkUI_KeySourceType source_type = OH_ArkUI_KeyEvent_GetKeySource(p_event);
	if (source_type == ARKUI_KEY_SOURCE_TYPE_KEYBOARD) {
		const ArkUI_KeyEventType key_type = OH_ArkUI_KeyEvent_GetType(p_event);
		if (key_type != ARKUI_KEY_EVENT_DOWN && key_type != ARKUI_KEY_EVENT_UP && key_type != ARKUI_KEY_EVENT_LONG_PRESS) {
			return;
		}

		Ref<InputEventKey> ke;
		ke.instantiate();

		ke->set_pressed(key_type != ARKUI_KEY_EVENT_UP);
		ke->set_echo(key_type == ARKUI_KEY_EVENT_LONG_PRESS);

		const int32_t keysym = OH_ArkUI_KeyEvent_GetKeyCode(p_event);
		ke->set_keycode(KeyMappingOpenHarmony::map_key(keysym));
		ke->set_location(KeyMappingOpenHarmony::get_location(keysym));

		const uint32_t unicode = OH_ArkUI_KeyEvent_GetUnicode(p_event);
		ke->set_unicode(unicode);
		const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(p_event);
		ke->set_device(device_id);
		// ke->set_window_id(0);

		_parse_modifiers_from(p_event, ke);

		Input::get_singleton()->parse_input_event(ke);
	}
}

void DisplayServerOpenHarmony::_input(ArkUI_NodeEvent *p_event) {
	if (!get_singleton()) {
		return;
	}
	ArkUI_UIInputEvent *source_event = OH_ArkUI_NodeEvent_GetInputEvent(p_event);
	if (!source_event) {
		return;
	}
	const int32_t type = OH_ArkUI_UIInputEvent_GetType(source_event);
	if (type == ARKUI_UIINPUTEVENT_TYPE_UNKNOWN) {
		return;
	}
	const ArkUI_NodeEventType event_type = OH_ArkUI_NodeEvent_GetEventType(p_event);
	// const int32_t native_window_id = OH_ArkUI_NodeEvent_GetTargetId(p_event);
	if (event_type == NODE_TOUCH_EVENT && type == ARKUI_UIINPUTEVENT_TYPE_TOUCH) {
		get_singleton()->_parse_touch_event(source_event);
	} else if (event_type == NODE_ON_AXIS && type == ARKUI_UIINPUTEVENT_TYPE_AXIS) {
		get_singleton()->_parse_axis_event(source_event);
	} else if (event_type == NODE_ON_MOUSE && type == ARKUI_UIINPUTEVENT_TYPE_MOUSE) {
		get_singleton()->_parse_mouse_event(source_event);
	} else if (event_type == NODE_ON_KEY_EVENT && type == ARKUI_UIINPUTEVENT_TYPE_KEY) {
		get_singleton()->_parse_key_event(source_event);
	}
}

DisplayServerOpenHarmony::NodeData::~NodeData() {
	OH_ArkUI_XComponent_UnregisterOnFrameCallback(node);
	if (parent) {
		OH_ArkUI_NodeContent_RemoveNode(parent, node);
	}

	if (holder) {
		if (callback) {
			OH_ArkUI_SurfaceHolder_RemoveSurfaceCallback(holder, callback);
			OH_ArkUI_SurfaceCallback_Dispose(callback);
		}

		OH_ArkUI_SurfaceHolder_Dispose(holder);
	}
}

void DisplayServerOpenHarmony::_surface_created_native(OH_ArkUI_SurfaceHolder *p_holder) {
}

void DisplayServerOpenHarmony::_surface_changed_native(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height) {
	ERR_PRINT(vformat("_surface_changed_native, =========== width: %d, height: %d.", p_width, p_height));
	get_singleton()->resize_window(p_width, p_height);
}

void DisplayServerOpenHarmony::_surface_destroyed_native(OH_ArkUI_SurfaceHolder *p_holder) {
}

void DisplayServerOpenHarmony::_surface_show_native(OH_ArkUI_SurfaceHolder *p_holder) {
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
	}
}

void DisplayServerOpenHarmony::_surface_hide_native(OH_ArkUI_SurfaceHolder *p_holder) {
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
	}
}

void DisplayServerOpenHarmony::_frame_callback_native(ArkUI_NodeHandle p_node, uint64_t p_timestamp, uint64_t p_target_timestamp) {
	ERR_FAIL_COND(!get_singleton()->node_datas.has(p_node));
	DisplayServer::get_singleton()->process_events();
	Main::iteration();
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
	r_error = ERR_INVALID_PARAMETER;
	rendering_driver = p_rendering_driver;
	ERR_FAIL_COND_MSG(rendering_driver != "vulkan", vformat("Failed to create %s context.", rendering_driver));

	r_error = ERR_UNAVAILABLE;
	KeyMappingOpenHarmony::initialize();

	native_menu = memnew(NativeMenu);

	screen_manager = memnew(ScreenManagerOpenharmony);
	input_manager = memnew(InputManagerOpenHarmony);

#ifdef VULKAN_ENABLED
	rendering_context = memnew(RenderingContextDriverVulkanOpenHarmony);
	if (rendering_context->initialize() != OK) {
		ERR_PRINT(vformat("Failed to initialize %s context.", rendering_driver));
		memdelete(rendering_context);
		rendering_context = nullptr;
		return;
	}

	RenderingContextDriverVulkanOpenHarmony::WindowPlatformData vulkan;
	OHNativeWindow *native_window = OS_OpenHarmony::get_singleton()->get_native_window();
	vulkan.window = native_window;

	if (!native_window || rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &vulkan) != OK) {
		ERR_PRINT(vformat("Failed to create %s window.", rendering_driver));
		memdelete(rendering_context);
		rendering_context = nullptr;
		return;
	}

	Size2i display_size = screen_get_size();
	ERR_PRINT(vformat("display size: %d, %d.", display_size.x, display_size.y));
	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, display_size.width, display_size.height);
	rendering_context->window_set_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID, p_vsync_mode);

	rendering_device = memnew(RenderingDevice);
	Error err = rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID);
	if (err != OK) {
		ERR_PRINT(vformat("Failed to initialize rendering device. error: %d.", err));
		memdelete(rendering_device);
		rendering_device = nullptr;
		return;
	}

	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);

	RendererCompositorRD::make_current();
#endif // VULKAN_ENABLED

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	r_error = OK;
}

DisplayServerOpenHarmony::~DisplayServerOpenHarmony() {
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}

	virtual_keyboard_hide(); // Ensure IME resources are cleaned up.

	if (rendering_device) {
		rendering_device->screen_free(DisplayServerEnums::MAIN_WINDOW_ID);
		memdelete(rendering_device);
		rendering_device = nullptr;
	}

	if (rendering_context) {
		rendering_context->window_destroy(DisplayServerEnums::MAIN_WINDOW_ID);
		memdelete(rendering_context);
		rendering_context = nullptr;
	}

	if (input_manager) {
		memdelete(input_manager);
		input_manager = nullptr;
	}

	if (screen_manager) {
		memdelete(screen_manager);
		screen_manager = nullptr;
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

void DisplayServerOpenHarmony::send_input_event(const Ref<InputEvent> &p_event) const {
	_window_callback(input_event_callback, p_event);
}

void DisplayServerOpenHarmony::resize_window(uint32_t p_width, uint32_t p_height) {
	Size2i size = Size2i(p_width, p_height);

#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, size.x, size.y);
	}
#endif // RD_ENABLED

	Variant resize_rect = Rect2i(Point2i(), size);
	_window_callback(window_resize_callback, resize_rect);
}

void DisplayServerOpenHarmony::send_window_event(DisplayServerEnums::WindowEvent p_event) const {
	_window_callback(window_event_callback, int(p_event));
}

bool DisplayServerOpenHarmony::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_CLIPBOARD:
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
		case DisplayServerEnums::FEATURE_IME:
		case DisplayServerEnums::FEATURE_KEEP_SCREEN_ON:
			return true;
		default:
			return false;
	}
}

String DisplayServerOpenHarmony::get_name() const {
	return "OpenHarmony";
}

TypedArray<Rect2> DisplayServerOpenHarmony::get_display_cutouts(int p_screen) const {
	return screen_manager->get_display_cutouts(p_screen);
}

int DisplayServerOpenHarmony::get_screen_count() const {
	return screen_manager->get_screen_count();
}

int DisplayServerOpenHarmony::get_primary_screen() const {
	return screen_manager->get_primary_screen();
}

int DisplayServerOpenHarmony::get_screen_from_rect(const Rect2 &p_rect) const {
	return screen_manager->get_screen_from_rect(p_rect);
}

Point2i DisplayServerOpenHarmony::screen_get_position(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_position(p_screen);
}

Size2i DisplayServerOpenHarmony::screen_get_size(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_size(p_screen);
}

Rect2i DisplayServerOpenHarmony::screen_get_usable_rect(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_usable_rect(p_screen);
}

int DisplayServerOpenHarmony::screen_get_dpi(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_dpi(p_screen);
}

float DisplayServerOpenHarmony::screen_get_scale(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_scale(p_screen);
}

float DisplayServerOpenHarmony::screen_get_refresh_rate(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_refresh_rate(p_screen);
}

Color DisplayServerOpenHarmony::screen_get_pixel(const Point2i &p_position) const {
	return screen_manager->screen_get_pixel(p_position);
}

Ref<Image> DisplayServerOpenHarmony::screen_get_image(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_image(p_screen);
}

Ref<Image> DisplayServerOpenHarmony::screen_get_image_rect(const Rect2i &p_rect) const {
	return screen_manager->screen_get_image_rect(p_rect);
}

bool DisplayServerOpenHarmony::is_touchscreen_available() const {
	return screen_manager->is_touchscreen_available();
}

void DisplayServerOpenHarmony::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	p_screen = _get_screen_index(p_screen);
	screen_manager->screen_set_orientation(p_orientation, p_screen);
}

DisplayServerEnums::ScreenOrientation DisplayServerOpenHarmony::screen_get_orientation(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_orientation(p_screen);
}

void DisplayServerOpenHarmony::screen_set_keep_on(bool p_enable) {
	ohos_wrapper_screen_set_keep_on(OS_OpenHarmony::get_singleton()->get_native_main_window_id(), p_enable);
}

bool DisplayServerOpenHarmony::screen_is_kept_on() const {
	return ohos_wrapper_screen_is_kept_on(OS_OpenHarmony::get_singleton()->get_native_main_window_id());
}

void DisplayServerOpenHarmony::mouse_set_mode(DisplayServerEnums::MouseMode p_mode) {
	input_manager->mouse_set_mode(p_mode);
}

DisplayServerEnums::MouseMode DisplayServerOpenHarmony::mouse_get_mode() const {
	return input_manager->mouse_get_mode();
}

void DisplayServerOpenHarmony::mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) {
	input_manager->mouse_set_mode_override(p_mode);
}

DisplayServerEnums::MouseMode DisplayServerOpenHarmony::mouse_get_mode_override() const {
	return input_manager->mouse_get_mode_override();
}

void DisplayServerOpenHarmony::mouse_set_mode_override_enabled(bool p_override_enabled) {
	input_manager->mouse_set_mode_override_enabled(p_override_enabled);
}

bool DisplayServerOpenHarmony::mouse_is_mode_override_enabled() const {
	return input_manager->mouse_is_mode_override_enabled();
}

void DisplayServerOpenHarmony::warp_mouse(const Point2i &p_position) {
	input_manager->warp_mouse(p_position);
}

Point2i DisplayServerOpenHarmony::mouse_get_position() const {
	return input_manager->mouse_get_position();
}

BitField<MouseButtonMask> DisplayServerOpenHarmony::mouse_get_button_state() const {
	return input_manager->mouse_get_button_state();
}

void DisplayServerOpenHarmony::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	input_manager->cursor_set_shape(p_shape);
}

DisplayServerEnums::CursorShape DisplayServerOpenHarmony::cursor_get_shape() const {
	return input_manager->cursor_get_shape();
}

void DisplayServerOpenHarmony::cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
	if (p_cursor.is_null()) {
		input_manager->cursor_set_custom_image(p_cursor, p_shape, p_hotspot);
		return;
	}

	Ref<Image> image = _get_cursor_image_from_resource(p_cursor, p_hotspot);
	ERR_FAIL_COND(image.is_null());

	input_manager->cursor_set_custom_image(p_cursor, p_shape, p_hotspot);
}

Point2i DisplayServerOpenHarmony::ime_get_selection() const {
	return input_manager->ime_get_selection();
}

String DisplayServerOpenHarmony::ime_get_text() const {
	return input_manager->ime_get_text();
}

void DisplayServerOpenHarmony::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	input_manager->virtual_keyboard_show(p_existing_text, p_screen_rect, p_type, p_max_length, p_cursor_start, p_cursor_end);
}

void DisplayServerOpenHarmony::virtual_keyboard_hide() {
	input_manager->virtual_keyboard_hide();
}

int DisplayServerOpenHarmony::virtual_keyboard_get_height() const {
	return input_manager->virtual_keyboard_get_height();
}

void DisplayServerOpenHarmony::window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window) {
	input_manager->window_set_ime_active(p_active, p_window);
}

void DisplayServerOpenHarmony::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	input_manager->window_set_ime_position(p_pos, p_window);
}

void DisplayServerOpenHarmony::clipboard_set(const String &p_text) {
	input_manager->clipboard_set(p_text);
}

String DisplayServerOpenHarmony::clipboard_get() const {
	return input_manager->clipboard_get();
}

Ref<Image> DisplayServerOpenHarmony::clipboard_get_image() const {
	return input_manager->clipboard_get_image();
}

bool DisplayServerOpenHarmony::clipboard_has() const {
	return input_manager->clipboard_has_text();
}

bool DisplayServerOpenHarmony::clipboard_has_image() const {
	return input_manager->clipboard_has_image();
}

Vector<DisplayServerEnums::WindowID> DisplayServerOpenHarmony::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> ret;
	ret.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return ret;
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
	window_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_event_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	input_text_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	window_resize_callback = p_callable;
}

void DisplayServerOpenHarmony::window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

void DisplayServerOpenHarmony::window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

int DisplayServerOpenHarmony::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, DisplayServerEnums::INVALID_SCREEN);
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
	// Not supported on OpenHarmony.
}

Size2i DisplayServerOpenHarmony::window_get_size(DisplayServerEnums::WindowID p_window) const {
	return Size2i(1080, 720);
	// return OS_OpenHarmony::get_singleton()->get_display_size();
}

Size2i DisplayServerOpenHarmony::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return Size2i(1080, 720);
	// return OS_OpenHarmony::get_singleton()->get_display_size();
}

void DisplayServerOpenHarmony::window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::WindowMode DisplayServerOpenHarmony::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WINDOW_MODE_FULLSCREEN;
}

void DisplayServerOpenHarmony::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::VSyncMode DisplayServerOpenHarmony::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::VSYNC_ADAPTIVE;
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
