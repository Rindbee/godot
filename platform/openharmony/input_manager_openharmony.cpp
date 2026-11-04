/**************************************************************************/
/*  input_manager_openharmony.cpp                                        */
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

#include "input_manager_openharmony.h"

#include "display_server_openharmony.h"
#include "key_mapping_openharmony.h"
#include "napi_bridge.h"
#include "os_openharmony.h"
#include "pixelmap_driver.h"
#include "screen_manager_openharmony.h"

#include "core/input/input.h"
#include "core/object/callable_mp.h"

#include <arkui/native_key_event.h>
#include <arkui/native_node.h>
#include <database/pasteboard/oh_pasteboard.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <multimodalinput/oh_input_manager.h>

namespace OpenHarmony {
#include <window_manager/oh_window.h>
} // namespace OpenHarmony

InputManagerOpenHarmony *InputManagerOpenHarmony::singleton = nullptr;

InputManagerOpenHarmony::InputManagerOpenHarmony() {
	singleton = this;
}

InputManagerOpenHarmony::~InputManagerOpenHarmony() {
	virtual_keyboard_hide(); // Ensure IME resources are cleaned up.

	if (custom_cursor) {
		OH_PixelmapNative *pixelmap = nullptr;
		OH_Input_CustomCursor_GetPixelMap(custom_cursor, &pixelmap);
		OH_Input_CustomCursor_Destroy(&custom_cursor);
		OH_PixelmapNative_Destroy(&pixelmap);
		custom_cursor = nullptr;
	}

	singleton = nullptr;
}

void InputManagerOpenHarmony::_parse_modifiers_from(const ArkUI_UIInputEvent *p_source_event, const Ref<InputEventWithModifiers> &p_event) {
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

MouseButton InputManagerOpenHarmony::_map_mouse_button(int32_t p_button) {
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

void InputManagerOpenHarmony::_parse_mouse_event_from(ArkUI_UIInputEvent *p_source_event, const Ref<InputEventMouse> &p_event) {
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
	pos = ScreenManagerOpenHarmony::get_singleton()->vp_to_px(pos, DisplayServerEnums::SCREEN_OF_MAIN_WINDOW);
	p_event->set_position(pos);
	p_event->set_global_position(pos);

	// Device and window.
	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(p_source_event);
	p_event->set_device(device_id);
	p_event->set_window_id(0);

	// Modifiers.
	_parse_modifiers_from(p_source_event, p_event);
}

void InputManagerOpenHarmony::_parse_touch_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
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

	pos = ScreenManagerOpenHarmony::get_singleton()->vp_to_px(pos, DisplayServerEnums::SCREEN_OF_MAIN_WINDOW);
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

	Input::get_singleton()->parse_input_event(ste);
}

void InputManagerOpenHarmony::_parse_axis_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
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

void InputManagerOpenHarmony::_parse_mouse_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
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

void InputManagerOpenHarmony::_parse_key_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window) {
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

void InputManagerOpenHarmony::node_event_receiver(ArkUI_NodeEvent *p_event) {
	ERR_FAIL_NULL(get_singleton());
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_NodeEvent_GetUserData(p_event));
	ERR_FAIL_NULL(nd);
	ArkUI_UIInputEvent *source_event = OH_ArkUI_NodeEvent_GetInputEvent(p_event);
	ERR_FAIL_NULL(source_event);

	const int32_t type = OH_ArkUI_UIInputEvent_GetType(source_event);
	if (type == ARKUI_UIINPUTEVENT_TYPE_UNKNOWN) {
		return;
	}
	// const int32_t display_id = OH_ArkUI_UIInputEvent_GetTargetDisplayId(source_event);
	// const int32_t native_window_id = OH_ArkUI_NodeEvent_GetTargetId(p_event);
	const int32_t device_id = OH_ArkUI_UIInputEvent_GetDeviceId(source_event);

	const ArkUI_NodeEventType event_type = OH_ArkUI_NodeEvent_GetEventType(p_event);

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

void InputManagerOpenHarmony::_mouse_update_mode() {
}

void InputManagerOpenHarmony::mouse_set_mode(DisplayServerEnums::MouseMode p_mode) {
	ERR_FAIL_INDEX(p_mode, DisplayServerEnums::MouseMode::MOUSE_MODE_MAX);
	if (p_mode == mouse_mode_base) {
		return;
	}
	mouse_mode_base = p_mode;
	_mouse_update_mode();
}

DisplayServerEnums::MouseMode InputManagerOpenHarmony::mouse_get_mode() const {
	return mouse_mode_base;
}

void InputManagerOpenHarmony::mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) {
	ERR_FAIL_INDEX(p_mode, DisplayServerEnums::MouseMode::MOUSE_MODE_MAX);
	if (p_mode == mouse_mode_override) {
		return;
	}
	mouse_mode_override = p_mode;
	_mouse_update_mode();
}

DisplayServerEnums::MouseMode InputManagerOpenHarmony::mouse_get_mode_override() const {
	return mouse_mode_override;
}

void InputManagerOpenHarmony::mouse_set_mode_override_enabled(bool p_override_enabled) {
	if (p_override_enabled == mouse_mode_override_enabled) {
		return;
	}
	mouse_mode_override_enabled = p_override_enabled;
	_mouse_update_mode();
}

bool InputManagerOpenHarmony::mouse_is_mode_override_enabled() const {
	return mouse_mode_override_enabled;
}

void InputManagerOpenHarmony::_cursor_update_shape(bool p_update_custom) {
	HashMap<DisplayServerEnums::CursorShape, CustomCursor>::ConstIterator I = custom_cursors.find(cursor_shape);
	if (!I) {
		for (const KeyValue<DisplayServerEnums::WindowID, DisplayServerOpenHarmony::WindowData> &E : DisplayServerOpenHarmony::get_singleton()->windows) {
			OH_Input_SetPointerStyle(E.value.native_window_id, cursors[cursor_shape]);
		}
		return;
	}

	if (!p_update_custom && last_cursor_shape == cursor_shape && last_cursor_shape != DisplayServerEnums::CURSOR_MAX) {
		for (const KeyValue<DisplayServerEnums::WindowID, DisplayServerOpenHarmony::WindowData> &E : DisplayServerOpenHarmony::get_singleton()->windows) {
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

	for (const KeyValue<DisplayServerEnums::WindowID, DisplayServerOpenHarmony::WindowData> &E : DisplayServerOpenHarmony::get_singleton()->windows) {
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

void InputManagerOpenHarmony::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	ERR_FAIL_INDEX(p_shape, DisplayServerEnums::CURSOR_MAX);
	if (cursor_shape == p_shape) {
		return;
	}
	cursor_shape = p_shape;

	_cursor_update_shape();
}

DisplayServerEnums::CursorShape InputManagerOpenHarmony::cursor_get_shape() const {
	return cursor_shape;
}

void InputManagerOpenHarmony::cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
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

Point2i InputManagerOpenHarmony::ime_get_selection() const {
	return config.im_selection;
}

String InputManagerOpenHarmony::ime_get_text() const {
	return im_text;
}

void InputManagerOpenHarmony::_input_text_key(Key p_key, char32_t p_char, bool p_pressed, bool p_ctrl_pressed) {
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

void InputManagerOpenHarmony::_clear_text_preview() {
	if (config.im_selection.y == 0) {
		return;
	}
	im_text = String();
	config.im_selection.y = 0;
	callable_mp((Object *)OS_OpenHarmony::get_singleton()->get_main_loop(), &Object::notification).call_deferred(MainLoop::NOTIFICATION_OS_IME_UPDATE, false);
}

void InputManagerOpenHarmony::_get_text_config(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_TextConfig *p_text_config) {
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
	// ERR_PRINT(vformat("_get_text_config,input_type:%d,enter_key_type:%d.", input_type, enter_key_type));
	OH_TextConfig_SetInputType(p_text_config, input_type);
	OH_TextConfig_SetPreviewTextSupport(p_text_config, true);
	OH_TextConfig_SetEnterKeyType(p_text_config, enter_key_type);
}

void InputManagerOpenHarmony::_insert_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length) {
	const String chars = String::utf16(p_text, p_length);
	// ERR_PRINT(vformat("insert text:%s,length:%d.", chars, p_length));
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

void InputManagerOpenHarmony::_delete_forward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	get_singleton()->_clear_text_preview();
	for (int32_t i = 0; i < p_length; i++) {
		_input_text_key(Key::KEY_DELETE, 0, true);
		_input_text_key(Key::KEY_DELETE, 0, false);
	}
}

void InputManagerOpenHarmony::_delete_backward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	get_singleton()->_clear_text_preview();
	get_singleton()->config.im_selection.x = MAX(0, get_singleton()->config.im_selection.x - p_length);
	for (int32_t i = 0; i < p_length; i++) {
		_input_text_key(Key::BACKSPACE, 0, true);
		_input_text_key(Key::BACKSPACE, 0, false);
	}
}

void InputManagerOpenHarmony::_send_keyboard_status(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_KeyboardStatus p_status) {
	// ERR_PRINT(vformat("_send_keyboard_status,type:%d.", p_status));
	get_singleton()->keyboard_visible = p_status == IME_KEYBOARD_STATUS_SHOW;
}

void InputManagerOpenHarmony::_send_enter_key(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_EnterKeyType p_enter_key_type) {
	// ERR_PRINT(vformat("_send_enter_key,type:%d.", p_enter_key_type));
	get_singleton()->_clear_text_preview();
	_input_text_key(Key::ENTER, 0, true);
	_input_text_key(Key::ENTER, 0, false);
}

void InputManagerOpenHarmony::_move_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_Direction p_direction) {
	// ERR_PRINT(vformat("_move_cursor,direction:%d.", p_direction));
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

void InputManagerOpenHarmony::_handle_set_selection(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_start, int32_t p_end) {
	// ERR_PRINT(vformat("_handle_set_selection,start:%d,end:%d.", p_start, p_end));
}

void InputManagerOpenHarmony::_handle_extend_action(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_ExtendAction p_action) {
	// ERR_PRINT(vformat("_handle_extend_action, p_action:%d.", p_action));
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

void InputManagerOpenHarmony::_get_left_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	// ERR_PRINT(vformat("_get_left_text_of_cursor,im_text:%s,im_selection:%v, length:%d, number:%d,text:%s.", get_singleton()->im_text, get_singleton()->config.im_selection, *p_length, p_number, String::utf16(p_text, *p_length)));
}

void InputManagerOpenHarmony::_get_right_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	// ERR_PRINT(vformat("_get_right_text_of_cursor=======,im_text:%s,im_selection:%v, length:%d, number:%d,text:%s.", get_singleton()->im_text, get_singleton()->config.im_selection, *p_length, p_number, String::utf16(p_text, *p_length)));
}

int32_t InputManagerOpenHarmony::_get_text_index_at_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	return 0;
}

int32_t InputManagerOpenHarmony::_receive_private_command(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_PrivateCommand *p_command[], size_t p_length) {
	return 0;
}

int32_t InputManagerOpenHarmony::_set_preview_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length, int32_t p_start, int32_t p_end) {
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

void InputManagerOpenHarmony::_finish_text_preview(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	get_singleton()->_clear_text_preview();
}

void InputManagerOpenHarmony::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (keyboard_visible && config.keyboard_type == p_type) {
		return;
	}

	if (keyboard_visible) {
		virtual_keyboard_hide();
	}

	config.im_selection.x = p_cursor_start;
	config.im_selection.y = MAX(p_cursor_end - p_cursor_start, 0);

	// ERR_PRINT(vformat("text:%s,position:%v,size:%v,max length:%d,cursor start:%d,cursor end:%d", p_existing_text, p_screen_rect.position, p_screen_rect.size, p_max_length, p_cursor_start, p_cursor_end));

	config.keyboard_type = p_type;
	text_editor_proxy = OH_TextEditorProxy_Create();
	attach_options = OH_AttachOptions_Create(true);

	OH_TextEditorProxy_SetGetTextConfigFunc(text_editor_proxy, &InputManagerOpenHarmony::_get_text_config);
	OH_TextEditorProxy_SetInsertTextFunc(text_editor_proxy, &InputManagerOpenHarmony::_insert_text);
	OH_TextEditorProxy_SetDeleteForwardFunc(text_editor_proxy, &InputManagerOpenHarmony::_delete_forward);
	OH_TextEditorProxy_SetDeleteBackwardFunc(text_editor_proxy, &InputManagerOpenHarmony::_delete_backward);
	OH_TextEditorProxy_SetSendKeyboardStatusFunc(text_editor_proxy, &InputManagerOpenHarmony::_send_keyboard_status);
	OH_TextEditorProxy_SetSendEnterKeyFunc(text_editor_proxy, &InputManagerOpenHarmony::_send_enter_key);
	OH_TextEditorProxy_SetMoveCursorFunc(text_editor_proxy, &InputManagerOpenHarmony::_move_cursor);
	OH_TextEditorProxy_SetHandleSetSelectionFunc(text_editor_proxy, &InputManagerOpenHarmony::_handle_set_selection);
	OH_TextEditorProxy_SetHandleExtendActionFunc(text_editor_proxy, &InputManagerOpenHarmony::_handle_extend_action);
	OH_TextEditorProxy_SetGetLeftTextOfCursorFunc(text_editor_proxy, &InputManagerOpenHarmony::_get_left_text_of_cursor);
	OH_TextEditorProxy_SetGetRightTextOfCursorFunc(text_editor_proxy, &InputManagerOpenHarmony::_get_right_text_of_cursor);
	OH_TextEditorProxy_SetGetTextIndexAtCursorFunc(text_editor_proxy, &InputManagerOpenHarmony::_get_text_index_at_cursor);
	OH_TextEditorProxy_SetReceivePrivateCommandFunc(text_editor_proxy, &InputManagerOpenHarmony::_receive_private_command);
	OH_TextEditorProxy_SetSetPreviewTextFunc(text_editor_proxy, &InputManagerOpenHarmony::_set_preview_text);
	OH_TextEditorProxy_SetFinishTextPreviewFunc(text_editor_proxy, &InputManagerOpenHarmony::_finish_text_preview);

	InputMethod_ErrorCode code = OH_InputMethodController_Attach(text_editor_proxy, attach_options, &input_method_proxy);

	if (code != IME_ERR_OK) {
		OH_TextEditorProxy_Destroy(text_editor_proxy);
		text_editor_proxy = nullptr;
		OH_AttachOptions_Destroy(attach_options);
		attach_options = nullptr;
		ERR_FAIL_MSG(vformat("Failed to attach input method controller: %d.", code));
	}
}

void InputManagerOpenHarmony::virtual_keyboard_hide() {
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

int InputManagerOpenHarmony::virtual_keyboard_get_height() const {
	if (keyboard_visible) {
		OpenHarmony::WindowManager_AvoidArea area;
		OpenHarmony::OH_WindowManager_GetWindowAvoidArea(DisplayServerOpenHarmony::main_native_window_id, OpenHarmony::WINDOW_MANAGER_AVOID_AREA_TYPE_KEYBOARD, &area);
		return area.bottomRect.height;
	}
	return 0;
}

void InputManagerOpenHarmony::window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window) {
	if (ime_active == p_active) {
		return;
	}
	ime_active = p_active;
	if (!ime_active && keyboard_visible) {
		virtual_keyboard_hide();
	}
}

void InputManagerOpenHarmony::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	if (ime_active && keyboard_visible) {
		InputMethod_CursorInfo *info = OH_CursorInfo_Create(p_pos.x, p_pos.y, 0, 30);
		OH_InputMethodProxy_NotifyCursorUpdate(input_method_proxy, info);
		OH_CursorInfo_Destroy(info);
	}
}

void InputManagerOpenHarmony::clipboard_set(const String &p_text) {
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

String InputManagerOpenHarmony::clipboard_get() const {
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

Ref<Image> InputManagerOpenHarmony::clipboard_get_image() const {
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

bool InputManagerOpenHarmony::clipboard_has(const char *p_type) const {
	OH_Pasteboard *pasteboard = OH_Pasteboard_Create();
	bool has_type = OH_Pasteboard_HasType(pasteboard, p_type);
	OH_Pasteboard_Destroy(pasteboard);
	return has_type;
}
