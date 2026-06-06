/**************************************************************************/
/*  input_manager_openharmony.cpp                                         */
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

#include "os_openharmony.h"
#include "pixelmap_driver.h"
#include "wrapper_openharmony.h"

#include "core/input/input.h"

#include <database/pasteboard/oh_pasteboard.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <multimodalinput/oh_input_manager.h>

#include <cstdint>

InputManagerOpenHarmony *InputManagerOpenHarmony::singleton = nullptr;

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

void InputManagerOpenHarmony::warp_mouse(const Point2i &p_position) {
}

Point2i InputManagerOpenHarmony::mouse_get_position() const {
	return Input::get_singleton()->get_mouse_position();
}

BitField<MouseButtonMask> InputManagerOpenHarmony::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

void InputManagerOpenHarmony::_cursor_update_shape(bool p_update_custom) {
	const int32_t window_id = OS_OpenHarmony::get_singleton()->get_native_main_window_id();
	HashMap<DisplayServerEnums::CursorShape, CustomCursor>::ConstIterator I = custom_cursors.find(cursor_shape);
	if (!I) {
		OH_Input_SetPointerStyle(window_id, cursors[cursor_shape]);
		return;
	}

	if (!p_update_custom && last_cursor_shape == cursor_shape && last_cursor_shape != DisplayServerEnums::CURSOR_MAX) {
		OH_Input_SetPointerStyle(window_id, DEVELOPER_DEFINED_ICON);
		return;
	}

	Input_CursorConfig *cursor_config = OH_Input_CursorConfig_Create(false);
	ERR_FAIL_NULL(cursor_config);

	OH_PixelmapNative *pixelmap = nullptr;
	Error err = PixelmapDriver::image_to_pixelmap(I->value.image, pixelmap);
	if (err != OK) {
		OH_PixelmapNative_Destroy(&pixelmap);
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

	Input_Result error = OH_Input_SetCustomCursor(window_id, cursor, cursor_config);
	if (error != INPUT_SUCCESS) {
		OH_Input_CustomCursor_Destroy(&cursor);
		OH_PixelmapNative_Destroy(&pixelmap);
		OH_Input_CursorConfig_Destroy(&cursor_config);
		ERR_PRINT(vformat("Failed to set custom cursor for shape %d with Input_Result: %d.", cursor_shape, error));
	}

	OH_Input_SetPointerStyle(window_id, DEVELOPER_DEFINED_ICON);

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

void InputManagerOpenHarmony::cursor_set_custom_image(const Ref<Image> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
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
	return config.ime_selection;
}

String InputManagerOpenHarmony::ime_get_text() const {
	return ime_text;
}

void InputManagerOpenHarmony::_input_text_key(Key p_key, char32_t p_char, Key p_unshifted, Key p_physical, int p_modifier, bool p_pressed, KeyLocation p_location) {
	Ref<InputEventKey> ev;
	ev.instantiate();
	ev->set_echo(false);
	ev->set_pressed(p_pressed);
	ev->set_keycode(fix_keycode(p_char, p_key));
	ev->set_key_label(p_unshifted);
	ev->set_physical_keycode(p_physical);
	ev->set_unicode(fix_unicode(p_char));
	ev->set_location(p_location);
	Input::get_singleton()->parse_input_event(ev);
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
	OH_TextConfig_SetInputType(p_text_config, input_type);
	OH_TextConfig_SetPreviewTextSupport(p_text_config, false);
	OH_TextConfig_SetEnterKeyType(p_text_config, enter_key_type);
}

void InputManagerOpenHarmony::_insert_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length) {
	const uint64_t old_size = singleton->config.text_buffer.size();
	singleton->config.text_buffer.resize(old_size + p_length);
	memcpy(singleton->config.text_buffer.ptrw() + old_size, p_text, p_length * sizeof(char16_t));
}

void InputManagerOpenHarmony::_delete_forward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	const int32_t old_size = singleton->config.text_buffer.size();
	p_length = CLAMP(p_length, 0, old_size);
	if (p_length == 0) {
		return;
	}
	char16_t *ptrw = singleton->config.text_buffer.ptrw();
	const int32_t new_size = old_size - p_length;
	if (new_size <= p_length) {
		memcpy(ptrw, ptrw + p_length, new_size * sizeof(char16_t));
	} else {
		memmove(ptrw, ptrw + p_length, new_size * sizeof(char16_t));
	}
	singleton->config.text_buffer.resize(new_size);
}

void InputManagerOpenHarmony::_delete_backward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length) {
	const int32_t old_size = singleton->config.text_buffer.size();
	p_length = CLAMP(p_length, 0, old_size);
	singleton->config.text_buffer.resize(old_size - p_length);
}

void InputManagerOpenHarmony::_send_keyboard_status(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_KeyboardStatus p_status) {
	singleton->keyboard_visible = p_status == IME_KEYBOARD_STATUS_SHOW;
}

void InputManagerOpenHarmony::_send_enter_key(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_EnterKeyType p_enter_key_type) {
	switch (p_enter_key_type) {
		case IME_ENTER_KEY_UNSPECIFIED:
		case IME_ENTER_KEY_NEWLINE: {
			singleton->config.text_buffer.append('\n');
		} break;
		case IME_ENTER_KEY_NONE:
		case IME_ENTER_KEY_GO:
		case IME_ENTER_KEY_SEARCH:
		case IME_ENTER_KEY_SEND:
		case IME_ENTER_KEY_NEXT:
		case IME_ENTER_KEY_DONE:
		case IME_ENTER_KEY_PREVIOUS:
		default: {
		} break;
	}
}

void InputManagerOpenHarmony::_move_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_Direction p_direction) {
	switch (p_direction) {
		case IME_DIRECTION_LEFT:
			_input_text_key(Key::LEFT, 0, Key::LEFT, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::LEFT, 0, Key::LEFT, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_RIGHT:
			_input_text_key(Key::RIGHT, 0, Key::RIGHT, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::RIGHT, 0, Key::RIGHT, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_UP:
			_input_text_key(Key::UP, 0, Key::UP, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::UP, 0, Key::UP, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		case IME_DIRECTION_DOWN:
			_input_text_key(Key::DOWN, 0, Key::DOWN, Key::NONE, 0, true, KeyLocation::UNSPECIFIED);
			_input_text_key(Key::DOWN, 0, Key::DOWN, Key::NONE, 0, false, KeyLocation::UNSPECIFIED);
			break;
		default:
			break;
	}
}

void InputManagerOpenHarmony::_handle_set_selection(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_start, int32_t p_end) {
	p_start = CLAMP(p_start, 0, singleton->config.text_buffer.size());
	p_end = CLAMP(p_end, 0, singleton->config.text_buffer.size());
	if (p_start > p_end) {
		SWAP(p_start, p_end);
	}
	singleton->config.ime_selection = Point2i(p_start, p_end - p_start);
	singleton->ime_text = String::utf16(singleton->config.text_buffer.ptr() + p_start, p_end - p_start);
}

void InputManagerOpenHarmony::_handle_extend_action(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_ExtendAction p_action) {
	// Not supported by Godot.
}

void InputManagerOpenHarmony::_get_left_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	// Not supported by Godot.
}

void InputManagerOpenHarmony::_get_right_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length) {
	// Not supported by Godot.
}

int32_t InputManagerOpenHarmony::_get_text_index_at_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	// Not supported by Godot.
	return 0;
}

int32_t InputManagerOpenHarmony::_receive_private_command(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_PrivateCommand *p_command[], size_t p_length) {
	// Not supported by Godot.
	return 0;
}

int32_t InputManagerOpenHarmony::_set_preview_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length, int32_t p_start, int32_t p_end) {
	// Not supported by Godot.
	return 0;
}

void InputManagerOpenHarmony::_finish_text_preview(InputMethod_TextEditorProxy *p_text_editor_proxy) {
	// Not supported by Godot.
}

void InputManagerOpenHarmony::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	if (keyboard_visible && config.keyboard_type == p_type) {
		return;
	}

	if (keyboard_visible) {
		virtual_keyboard_hide();
	}

	if (!p_existing_text.is_empty()) {
		Char16String text = p_existing_text.utf16();
		config.text_buffer.resize(text.size());
		memcpy(config.text_buffer.ptrw(), text.get_data(), text.size() * sizeof(char16_t));
	}

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
	config.text_buffer.clear();
}

int InputManagerOpenHarmony::virtual_keyboard_get_height() const {
	if (keyboard_visible) {
		int height = ohos_wrapper_get_keyboard_avoid_area(OS_OpenHarmony::get_singleton()->get_native_main_window_id());
		return height;
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

bool InputManagerOpenHarmony::clipboard_has_text() const {
	return clipboard_has(PASTEBOARD_MIMETYPE_TEXT_PLAIN);
}

bool InputManagerOpenHarmony::clipboard_has_image() const {
	return clipboard_has(PASTEBOARD_MIMETYPE_PIXELMAP);
}

InputManagerOpenHarmony::InputManagerOpenHarmony() {
	singleton = this;
}

InputManagerOpenHarmony::~InputManagerOpenHarmony() {
	singleton = nullptr;

	if (custom_cursor) {
		OH_PixelmapNative *pixelmap = nullptr;
		OH_Input_CustomCursor_GetPixelMap(custom_cursor, &pixelmap);
		OH_Input_CustomCursor_Destroy(&custom_cursor);
		OH_PixelmapNative_Destroy(&pixelmap);
		custom_cursor = nullptr;
	}

	virtual_keyboard_hide();
}
