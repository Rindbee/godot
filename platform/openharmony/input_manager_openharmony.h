/**************************************************************************/
/*  input_manager_openharmony.h                                           */
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

#include "core/io/image.h"
#include "servers/display/display_server_enums.h"

#include <inputmethod/inputmethod_types_capi.h>

struct Input_CustomCursor;
struct Input_CursorConfig;

struct InputMethod_AttachOptions;
struct InputMethod_TextEditorProxy;
struct InputMethod_InputMethodProxy;
struct InputMethod_TextConfig;
struct InputMethod_PrivateCommand;

class InputManagerOpenHarmony {
	bool mouse_mode_override_enabled = false;
	DisplayServerEnums::MouseMode mouse_mode_override = DisplayServerEnums::MOUSE_MODE_VISIBLE;
	DisplayServerEnums::MouseMode mouse_mode_base = DisplayServerEnums::MOUSE_MODE_VISIBLE;

	void _mouse_update_mode();

	int cursors[DisplayServerEnums::CURSOR_MAX] = {
		0, //DisplayServerEnums::CURSOR_ARROW
		26, //DisplayServerEnums::CURSOR_IBEAM
		19, //DisplayServerEnums::CURSOR_POINTIN
		13, //DisplayServerEnums::CURSOR_CROSS
		43, //DisplayServerEnums::CURSOR_WAIT
		42, //DisplayServerEnums::CURSOR_BUSY
		18, //DisplayServerEnums::CURSOR_DRAG
		17, //DisplayServerEnums::CURSOR_CAN_DRO
		15, //DisplayServerEnums::CURSOR_FORBIDD
		6, //DisplayServerEnums::CURSOR_VSIZE
		5, //DisplayServerEnums::CURSOR_HSIZE
		11, //DisplayServerEnums::CURSOR_BDIAGSI
		12, //DisplayServerEnums::CURSOR_FDIAGSI
		21, //DisplayServerEnums::CURSOR_MOVE
		23, //DisplayServerEnums::CURSOR_VSPLIT
		22, //DisplayServerEnums::CURSOR_HSPLIT
		20, //DisplayServerEnums::CURSOR_HELP
	};
	DisplayServerEnums::CursorShape cursor_shape = DisplayServerEnums::CursorShape::CURSOR_ARROW;
	DisplayServerEnums::CursorShape last_cursor_shape = DisplayServerEnums::CursorShape::CURSOR_MAX;

	struct CustomCursor {
		Ref<Image> image;
		Vector2 hotspot;
	};
	HashMap<DisplayServerEnums::CursorShape, CustomCursor> custom_cursors;
	Input_CustomCursor *custom_cursor = nullptr;

	void _cursor_update_shape(bool p_update_custom = false);

	struct TextConfig {
		DisplayServerEnums::VirtualKeyboardType keyboard_type = DisplayServerEnums::KEYBOARD_TYPE_DEFAULT;
		bool preview_text_supported = false;
		Point2i ime_selection;
		Vector<char16_t> text_buffer;
		int32_t window_id = 0;
		Vector<char16_t> placeholder;
	};

	bool ime_active = false;
	bool keyboard_visible = false;
	String ime_text;
	TextConfig config;
	InputMethod_TextEditorProxy *text_editor_proxy = nullptr;
	InputMethod_AttachOptions *attach_options = nullptr;
	InputMethod_InputMethodProxy *input_method_proxy = nullptr;

	static void _get_text_config(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_TextConfig *p_text_config);
	static void _insert_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length);
	static void _delete_forward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length);
	static void _delete_backward(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_length);
	static void _send_keyboard_status(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_KeyboardStatus p_keyboard_status);
	static void _send_enter_key(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_EnterKeyType p_enter_key_type);
	static void _move_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_Direction p_direction);
	static void _handle_set_selection(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_start, int32_t p_end);
	static void _handle_extend_action(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_ExtendAction p_action);
	static void _get_left_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length);
	static void _get_right_text_of_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy, int32_t p_number, char16_t *p_text, size_t *p_length);
	static int32_t _get_text_index_at_cursor(InputMethod_TextEditorProxy *p_text_editor_proxy);
	static int32_t _receive_private_command(InputMethod_TextEditorProxy *p_text_editor_proxy, InputMethod_PrivateCommand *p_command[], size_t p_length);
	static int32_t _set_preview_text(InputMethod_TextEditorProxy *p_text_editor_proxy, const char16_t *p_text, size_t p_length, int32_t p_start, int32_t p_end);
	static void _finish_text_preview(InputMethod_TextEditorProxy *p_text_editor_proxy);

	static void _input_text_key(Key p_key, char32_t p_char, Key p_unshifted, Key p_physical, int p_modifier, bool p_pressed, KeyLocation p_location);

	static InputManagerOpenHarmony *singleton;

public:
	/* MOUSE */

	void mouse_set_mode(DisplayServerEnums::MouseMode p_mode);
	DisplayServerEnums::MouseMode mouse_get_mode() const;
	void mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode);
	DisplayServerEnums::MouseMode mouse_get_mode_override() const;
	void mouse_set_mode_override_enabled(bool p_override_enabled);
	bool mouse_is_mode_override_enabled() const;

	void warp_mouse(const Point2i &p_position);
	Point2i mouse_get_position() const;
	BitField<MouseButtonMask> mouse_get_button_state() const;

	void cursor_set_shape(DisplayServerEnums::CursorShape p_shape);
	DisplayServerEnums::CursorShape cursor_get_shape() const;
	void cursor_set_custom_image(const Ref<Image> &p_cursor, DisplayServerEnums::CursorShape p_shape = DisplayServerEnums::CURSOR_ARROW, const Vector2 &p_hotspot = Vector2());

	/* KEYBOARD */

	Point2i ime_get_selection() const;
	String ime_get_text() const;

	void virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect = Rect2(), DisplayServerEnums::VirtualKeyboardType p_type = DisplayServerEnums::KEYBOARD_TYPE_DEFAULT, int p_max_length = -1, int p_cursor_start = -1, int p_cursor_end = -1);
	void virtual_keyboard_hide();
	int virtual_keyboard_get_height() const;

	/* INPUT METHOD */

	void window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID);
	void window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID);

	/* CLIPBOARD */

	void clipboard_set(const String &p_text);
	String clipboard_get() const;
	Ref<Image> clipboard_get_image() const;
	bool clipboard_has(const char *p_type) const;
	bool clipboard_has_text() const;
	bool clipboard_has_image() const;

	static InputManagerOpenHarmony *get_singleton() { return singleton; }

	InputManagerOpenHarmony();
	~InputManagerOpenHarmony();
};
