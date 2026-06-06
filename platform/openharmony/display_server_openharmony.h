/**************************************************************************/
/*  display_server_openharmony.h                                          */
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

#include "core/input/input_event.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_device.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <arkui/native_node.h>
#include <inputmethod/inputmethod_inputmethod_proxy_capi.h>
#include <inputmethod/inputmethod_text_editor_proxy_capi.h>
#include <multimodalinput/oh_input_manager.h>
#include <window_manager/oh_display_info.h>

class DisplayServerOpenHarmony : public DisplayServer {
	GDSOFTCLASS(DisplayServerOpenHarmony, DisplayServer);

private:
	friend class NAPIBridge;

	Point2 last_click_pos;
	uint64_t last_click_ms = 0;

	String rendering_driver;
	RenderingContextDriver *rendering_context = nullptr;
	RenderingDevice *rendering_device = nullptr;
	ObjectID window_attached_instance_id;

	void _window_callback(const Callable &p_callable, const Variant &p_arg, bool p_deferred = false) const;

	void _dispatch_input_event(const Ref<InputEvent> &p_event);
	static void dispatch_input_events(const Ref<InputEvent> &p_event);

	struct WindowData {
		DisplayServerEnums::WindowID id = DisplayServerEnums::INVALID_WINDOW_ID;
		int native_window_id = -1;
		DisplayServerEnums::WindowID parent_id = DisplayServerEnums::INVALID_WINDOW_ID;
		// For popups.
		DisplayServerEnums::WindowID root_id = DisplayServerEnums::INVALID_WINDOW_ID;
		// For toplevels.
		List<DisplayServerEnums::WindowID> popup_stack;
		Rect2i rect;
		Size2i max_size;
		Size2i min_size;
		Rect2i safe_rect;
		bool emulate_vsync = false;
		bool created = false;
		bool visible = false;
		DisplayServerEnums::VSyncMode vsync_mode = DisplayServerEnums::VSYNC_ENABLED;
		uint32_t flags = 0;
		DisplayServerEnums::WindowMode mode = DisplayServerEnums::WINDOW_MODE_WINDOWED;
		bool hdr_requested = false;
		NativeWindow *native_window = nullptr;
		Callable rect_changed_callback;
		Callable window_event_callback;
		Callable input_event_callback;
		Callable drop_files_callback;
		Callable input_text_callback;

		String title;
		ObjectID instance_id;
	};

	NativeMenu *native_menu = nullptr;
	DisplayServerEnums::Context context = DisplayServerEnums::CONTEXT_ENGINE;
	bool swap_cancel_ok = false;
	DisplayServerEnums::WindowID window_id_counter = DisplayServerEnums::MAIN_WINDOW_ID;
	HashMap<DisplayServerEnums::WindowID, WindowData> windows;

	static int main_native_window_id;
	static NativeWindow *main_native_window;

	void _surface_created(OH_ArkUI_SurfaceHolder *p_holder);
	void _surface_changed(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height);
	void _surface_destroyed(OH_ArkUI_SurfaceHolder *p_holder);
	void _surface_show(OH_ArkUI_SurfaceHolder *p_holder);
	void _surface_hide(OH_ArkUI_SurfaceHolder *p_holder);

	static void surface_created_callback(OH_ArkUI_SurfaceHolder *p_holder);
	static void surface_changed_callback(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height);
	static void surface_destroyed_callback(OH_ArkUI_SurfaceHolder *p_holder);
	static void surface_show_callback(OH_ArkUI_SurfaceHolder *p_holder);
	static void surface_hide_callback(OH_ArkUI_SurfaceHolder *p_holder);

	bool exited = false;
	static void frame_callback(ArkUI_Node *p_node, uint64_t p_timestamp, uint64_t p_target_timestamp);

	MouseButton _map_mouse_button(int32_t p_button);
	void _parse_mouse_event_from(ArkUI_UIInputEvent *p_source_event, const Ref<InputEventMouse> &p_event);
	void _parse_modifiers_from(const ArkUI_UIInputEvent *p_source_event, const Ref<InputEventWithModifiers> &p_event);

	void _parse_touch_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window);
	void _parse_axis_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window);
	void _parse_mouse_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window);
	void _parse_key_event(ArkUI_UIInputEvent *p_event, int p_device, DisplayServerEnums::WindowID p_window);

	Point2 mouse_pos;

	void _update_window_rect(const Rect2i &p_rect, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID);

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
		Point2i im_selection;
		int32_t window_id = 0;
	};

	bool ime_active = false;
	bool keyboard_visible = false;
	String im_text;
	TextConfig config;
	InputMethod_TextEditorProxy *text_editor_proxy = nullptr;
	InputMethod_AttachOptions *attach_options = nullptr;
	InputMethod_InputMethodProxy *input_method_proxy = nullptr;

	void _clear_text_preview();

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

	static void _input_text_key(Key p_key, char32_t p_char, bool p_pressed, bool p_ctrl_pressed = false);

	bool _clipboard_has(const char *p_type) const;

	enum ScreenRotation {
		SCREEN_ROTATION_0,
		SCREEN_ROTATION_90,
		SCREEN_ROTATION_180,
		SCREEN_ROTATION_270,
	};

	enum ScreenState {
		SCREEN_STATE_UNKNOWN = 0,
		SCREEN_STATE_OFF = 1,
		SCREEN_STATE_ON = 2,
		SCREEN_STATE_DOZE = 3,
		SCREEN_STATE_DOZE_SUSPEND = 4,
		SCREEN_STATE_VR = 5,
		SCREEN_STATE_ON_SUSPEND = 6,
	};

	enum FoldDisplayMode {
		FOLD_DISPLAY_MODE_UNKNOWN = 0,
		FOLD_DISPLAY_MODE_FULL = 1,
		FOLD_DISPLAY_MODE_MAIN = 2,
		FOLD_DISPLAY_MODE_SUB = 3,
		FOLD_DISPLAY_MODE_COORDINATION = 4,
	};

	enum DisplaySourceMode {
		DISPLAY_SOURCE_MODE_NONE = 0,
		DISPLAY_SOURCE_MODE_MAIN = 1,
		DISPLAY_SOURCE_MODE_MIRROR = 2,
		DISPLAY_SOURCE_MODE_EXTEND = 3,
		DISPLAY_SOURCE_MODE_ALONE = 4,
	};

	struct ScreenInfo {
		uint64_t id = 0;
		String name;
		bool alive = false;
		bool main_window_on = false;
		DisplaySourceMode source_mode = DISPLAY_SOURCE_MODE_NONE;
		Point2i global_position;
		Size2i size;
		Size2i physical_size;
		int refresh_rate = 0;
		Size2i available_size;
		Rect2i available_rect;
		float density_dpi = 0.0;
		float density_pixels = 0.0;
		float scaled_density = 0.0;
		Vector2 dpi;
		ScreenRotation rotation = SCREEN_ROTATION_0;
		ScreenState state = SCREEN_STATE_UNKNOWN;
		DisplayServerEnums::ScreenOrientation orientation = DisplayServerEnums::SCREEN_LANDSCAPE;
	};

	HashMap<uint64_t, ScreenInfo> screens;
	Vector<uint64_t> screen_ids;

	uint32_t screen_add_listener_idx = 0;
	uint32_t screen_change_listener_idx = 0;
	uint32_t screen_remove_listener_idx = 0;
	uint32_t available_area_change_listener_idx = 0;
	uint32_t fold_display_mode_change_listener_idx = 0;

	bool foldable = false;
	FoldDisplayMode fold_display_mode = FoldDisplayMode::FOLD_DISPLAY_MODE_UNKNOWN;

	Vector2 vp_to_px(Vector2 p_vp, int p_screen);

	void _update_display_info_from(ScreenInfo &p_screen, NativeDisplayManager_DisplayInfo *p_native_display);

	void _update_extra_info(ScreenInfo &p_screen_info);
	void _update_all_screen_info();

	static void display_add_or_change_callback(uint64_t p_display_id);
	static void display_remove_callback(uint64_t p_display_id);
	static void available_area_change_callback(uint64_t p_display_id);
	static void fold_display_mode_change_callback(NativeDisplayManager_FoldDisplayMode p_display_mode);

public:
	static DisplayServerOpenHarmony *get_singleton();
	static DisplayServer *create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);
	static Vector<String> get_rendering_drivers_func();
	static void register_openharmony_driver();

	static void node_event_receiver(ArkUI_NodeEvent *p_event);

	DisplayServerOpenHarmony(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);
	~DisplayServerOpenHarmony();

	virtual bool has_feature(DisplayServerEnums::Feature p_feature) const override;
	virtual String get_name() const override;

	/* SCREEN */

	virtual TypedArray<Rect2> get_display_cutouts(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	// virtual Rect2i get_display_safe_area() const { return screen_get_usable_rect(); }
	virtual int get_screen_count() const override;
	virtual int get_primary_screen() const override;
	// virtual int get_keyboard_focus_screen() const { return get_primary_screen(); }
	virtual int get_screen_from_rect(const Rect2 &p_rect) const override;
	virtual Point2i screen_get_position(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual Size2i screen_get_size(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual Rect2i screen_get_usable_rect(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual int screen_get_dpi(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual float screen_get_scale(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual float screen_get_refresh_rate(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual Color screen_get_pixel(const Point2i &p_position) const override;
	virtual Ref<Image> screen_get_image(int p_screen) const override;
	virtual Ref<Image> screen_get_image_rect(const Rect2i &p_rect) const override;
	virtual bool is_touchscreen_available() const override;
	virtual void screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) override;
	virtual DisplayServerEnums::ScreenOrientation screen_get_orientation(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override;
	virtual void screen_set_keep_on(bool p_enable) override;
	virtual bool screen_is_kept_on() const override;

	/* MOUSE */

	virtual void mouse_set_mode(DisplayServerEnums::MouseMode p_mode) override;
	virtual DisplayServerEnums::MouseMode mouse_get_mode() const override;
	virtual void mouse_set_mode_override(DisplayServerEnums::MouseMode p_mode) override;
	virtual DisplayServerEnums::MouseMode mouse_get_mode_override() const override;
	virtual void mouse_set_mode_override_enabled(bool p_override_enabled) override;
	virtual bool mouse_is_mode_override_enabled() const override;

	virtual void warp_mouse(const Point2i &p_position) override;
	virtual Point2i mouse_get_position() const override;
	virtual BitField<MouseButtonMask> mouse_get_button_state() const override;

	virtual void cursor_set_shape(DisplayServerEnums::CursorShape p_shape) override;
	virtual DisplayServerEnums::CursorShape cursor_get_shape() const override;
	virtual void cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape = DisplayServerEnums::CURSOR_ARROW, const Vector2 &p_hotspot = Vector2()) override;

	/* KEYBOARD */

	virtual Point2i ime_get_selection() const override;
	virtual String ime_get_text() const override;

	virtual void virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect = Rect2(), DisplayServerEnums::VirtualKeyboardType p_type = DisplayServerEnums::KEYBOARD_TYPE_DEFAULT, int p_max_length = -1, int p_cursor_start = -1, int p_cursor_end = -1) override;
	virtual void virtual_keyboard_hide() override;
	virtual int virtual_keyboard_get_height() const override;

	/* INPUT METHOD */

	virtual void window_set_ime_active(const bool p_active, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;

	/* CLIPBOARD */

	virtual void clipboard_set(const String &p_text) override;
	virtual String clipboard_get() const override;
	virtual Ref<Image> clipboard_get_image() const override;
	virtual bool clipboard_has() const override;
	virtual bool clipboard_has_image() const override;

	virtual void set_context(DisplayServerEnums::Context p_context) override;
	virtual bool get_swap_cancel_ok() override;

	virtual Vector<DisplayServerEnums::WindowID> get_window_list() const override;

	virtual DisplayServerEnums::WindowID create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect = Rect2i(), bool p_exclusive = false, DisplayServerEnums::WindowID p_transient_parent = DisplayServerEnums::INVALID_WINDOW_ID) override;
	virtual void show_window(DisplayServerEnums::WindowID p_window) override;
	virtual void delete_sub_window(DisplayServerEnums::WindowID p_window) override;

	virtual DisplayServerEnums::WindowID get_window_at_screen_position(const Point2i &p_position) const override;
	virtual void window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual ObjectID window_get_attached_instance_id(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_drop_files_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_title(const String &p_title, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual int window_get_current_screen(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual Point2i window_get_position(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual Point2i window_get_position_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_position(const Point2i &p_position, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_set_transient(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowID p_parent) override;
	virtual void window_set_max_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual Size2i window_get_max_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_min_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual Size2i window_get_min_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_size(const Size2i p_size, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual Size2i window_get_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual Size2i window_get_size_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_mode(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual DisplayServerEnums::WindowMode window_get_mode(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual DisplayServerEnums::VSyncMode window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const override;

	virtual bool window_is_hdr_output_supported(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	virtual void window_request_hdr_output(const bool p_enable, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual bool window_is_hdr_output_requested(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual bool window_is_hdr_output_enabled(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	virtual void window_set_hdr_output_reference_luminance(const float p_reference_luminance, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual float window_get_hdr_output_reference_luminance(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual float window_get_hdr_output_current_reference_luminance(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	virtual void window_set_hdr_output_max_luminance(const float p_max_luminance, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual float window_get_hdr_output_max_luminance(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual float window_get_hdr_output_current_max_luminance(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;

	virtual bool window_is_maximize_allowed(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_set_flag(DisplayServerEnums::WindowFlags p_flag, bool p_enabled, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual bool window_get_flag(DisplayServerEnums::WindowFlags p_flag, DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual void window_request_attention(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual void window_move_to_foreground(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) override;
	virtual bool window_is_focused(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual bool window_can_draw(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override;
	virtual bool can_any_window_draw() const override;
	virtual void process_events() override;
};
