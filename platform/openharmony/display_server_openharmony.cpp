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
#include "napi_bridge.h"
#include "os_openharmony.h"
#include "pixelmap_driver.h"
#include "screen_manager_openharmony.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/math/vector2i.h"
#include "core/object/callable_mp.h"
#include "main/main.h"
#include "servers/display/native_menu.h"

#ifdef RD_ENABLED
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#ifdef VULKAN_ENABLED
#include "rendering_context_driver_vulkan_openharmony.h"
#endif // VULKAN_ENABLED
#endif // RD_ENABLED

#ifdef GLES3_ENABLED
#include "egl_manager_openharmony.h"

#include "drivers/gles3/rasterizer_gles3.h"
#endif // GLES3_ENABLED

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <arkui/native_key_event.h>
#include <arkui/native_node.h>
#include <database/pasteboard/oh_pasteboard.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <multimodalinput/oh_input_manager.h>
#include <window_manager/oh_display_capture.h>
#include <window_manager/oh_display_manager.h>
namespace OpenHarmony {
#include <window_manager/oh_window.h>
} // namespace OpenHarmony

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
#endif // RD_ENABLED

#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->window_resize(p_window, wd.rect.size.width, wd.rect.size.height);
	}
#endif // GLES3_ENABLED

	if (wd.rect_changed_callback.is_valid()) {
		wd.rect_changed_callback.call(wd.rect);
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
	if (get_singleton()) {
		get_singleton()->_surface_created(p_holder);
		return;
	}
	NodeData *nd = reinterpret_cast<NodeData *>(OH_ArkUI_SurfaceHolder_GetUserData(p_holder));
	ERR_FAIL_NULL(nd);
	ERR_FAIL_COND(nd->window_id != -1);

	DisplayServerOpenHarmony::main_native_window = OH_ArkUI_XComponent_GetNativeWindow(p_holder);
	DisplayServerOpenHarmony::main_native_window_id = nd->native_window_id;

	NAPIBridge::_setup_engine(nd->holder);
	nd->window_id = get_singleton()->window_id_counter++;
}

void DisplayServerOpenHarmony::surface_changed_callback(OH_ArkUI_SurfaceHolder *p_holder, uint64_t p_width, uint64_t p_height) {
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_changed(p_holder, p_width, p_height);
}

void DisplayServerOpenHarmony::surface_destroyed_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_destroyed(p_holder);
}

void DisplayServerOpenHarmony::surface_show_callback(OH_ArkUI_SurfaceHolder *p_holder) {
	ERR_FAIL_NULL(get_singleton());
	get_singleton()->_surface_show(p_holder);
}

void DisplayServerOpenHarmony::surface_hide_callback(OH_ArkUI_SurfaceHolder *p_holder) {
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
#ifdef GLES3_ENABLED
	drivers.push_back("opengl3");
	drivers.push_back("opengl3_es");
#endif
#ifdef VULKAN_ENABLED
	drivers.push_back("vulkan");
#endif
	return drivers;
}

DisplayServer *DisplayServerOpenHarmony::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerOpenHarmony(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		if (p_rendering_driver == "vulkan") {
			OS::get_singleton()->alert(
					"Your device seems not to support the required Vulkan version.\n\n"
					"Please try exporting your game using the 'gl_compatibility' renderer.",
					"Unable to initialize Vulkan video driver");
		} else {
			OS::get_singleton()->alert(
					"Your device seems not to support the required OpenGL ES 3.0 version.",
					"Unable to initialize OpenGL video driver");
		}
	}
	return ds;
}

void DisplayServerOpenHarmony::register_openharmony_driver() {
	register_create_function("openharmony", create_func, get_rendering_drivers_func);
}

DisplayServerOpenHarmony::DisplayServerOpenHarmony(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	KeyMappingOpenHarmony::initialize();

	native_menu = memnew(NativeMenu);

	input_manager = memnew(InputManagerOpenHarmony);
	screen_manager = memnew(ScreenManagerOpenHarmony);

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
#ifdef GLES3_ENABLED
			bool fallback_to_opengl3 = GLOBAL_GET("rendering/rendering_device/fallback_to_opengl3");
			if (fallback_to_opengl3 && rendering_driver != "opengl3") {
				WARN_PRINT("Your video card drivers seem not to support the required Vulkan version, switching to OpenGL 3.");
				rendering_driver = "opengl3";
				OS::get_singleton()->set_current_rendering_method("gl_compatibility", OS::RENDERING_SOURCE_FALLBACK);
				OS::get_singleton()->set_current_rendering_driver_name(rendering_driver, OS::RENDERING_SOURCE_FALLBACK);
			} else
#endif // GLES3_ENABLED
			{
				r_error = ERR_CANT_CREATE;
				ERR_FAIL_MSG(vformat("Could not initialize %s", rendering_driver));
			}
		}
		driver_found = true;
	}
#endif // RD_ENABLED

#ifdef GLES3_ENABLED
	if (rendering_driver == "opengl3") {
		egl_manager = memnew(EGLManagerOpenHarmony);

		if (egl_manager->initialize() != OK || egl_manager->open_display(nullptr) != OK) {
			memdelete(egl_manager);
			egl_manager = nullptr;

			bool fallback = GLOBAL_GET("rendering/gl_compatibility/fallback_to_gles");
			if (fallback) {
				WARN_PRINT("Your video card drivers seem not to support the required OpenGL version, switching to OpenGLES.");
				rendering_driver = "opengl3_es";
				OS::get_singleton()->set_current_rendering_driver_name(rendering_driver, OS::RENDERING_SOURCE_FALLBACK);
			} else {
				r_error = ERR_UNAVAILABLE;

				OS::get_singleton()->alert(
						vformat("Your video card drivers seem not to support the required OpenGL 4.2 version.\n\n"
								"If possible, consider updating your video card drivers or using the Vulkan driver.\n\n"
								"You can enable the Vulkan driver by starting the engine from the\n"
								"command line with the command:\n\n     --rendering-driver vulkan\n\n"
								"If you recently updated your video card drivers, try rebooting."),
						"Unable to initialize OpenGL video driver");

				ERR_FAIL_MSG("Could not initialize OpenGL.");
			}
		} else {
			driver_found = true;
		}
	}

	if (rendering_driver == "opengl3_es") {
		egl_manager = memnew(EGLManagerOpenHarmonyGLES);

		if (egl_manager->initialize() != OK || egl_manager->open_display(nullptr) != OK) {
			memdelete(egl_manager);
			egl_manager = nullptr;
			r_error = ERR_CANT_CREATE;

			OS::get_singleton()->alert(
					vformat("Your video card drivers seem not to support the required OpenGL ES 3.2 version.\n\n"
							"If possible, consider updating your video card drivers or using the Vulkan driver.\n\n"
							"You can enable the Vulkan driver by starting the engine from the\n"
							"command line with the command:\n\n     --rendering-driver vulkan\n\n"
							"If you recently updated your video card drivers, try rebooting."),
					"Unable to initialize OpenGL ES video driver");

			ERR_FAIL_MSG("Could not initialize OpenGL ES.");
		}

		driver_found = true;
	}
#endif // GLES3_ENABLED

	if (!driver_found) {
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG(vformat("Video driver %s not found.", rendering_driver));
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

#if defined(GLES3_ENABLED)
	if (rendering_driver == "opengl3") {
		RasterizerGLES3::make_current(true);
	}
	if (rendering_driver == "opengl3_es") {
		RasterizerGLES3::make_current(false);
	}
#endif // GLES3_ENABLED

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

	OpenHarmony::OH_WindowManager_SetWindowKeepScreenOn(wd.native_window_id, GLOBAL_GET("display/window/energy_saving/keep_screen_on"));

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

TypedArray<Rect2> DisplayServerOpenHarmony::get_display_cutouts(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
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
	return true;
}

void DisplayServerOpenHarmony::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	if (p_screen != DisplayServerEnums::SCREEN_OF_MAIN_WINDOW && p_screen != _get_screen_index(DisplayServerEnums::SCREEN_OF_MAIN_WINDOW)) {
		ERR_PRINT(vformat("Cannot set the orientation for screen %d because only the main window is supported.", p_screen));
		return;
	}
}

DisplayServerEnums::ScreenOrientation DisplayServerOpenHarmony::screen_get_orientation(int p_screen) const {
	p_screen = _get_screen_index(p_screen);
	return screen_manager->screen_get_orientation(p_screen);
}

void DisplayServerOpenHarmony::screen_set_keep_on(bool p_enable) {
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		OpenHarmony::OH_WindowManager_SetWindowKeepScreenOn(E.value.native_window_id, p_enable);
	}
}

bool DisplayServerOpenHarmony::screen_is_kept_on() const {
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		if (E.value.is_keep_screen_on) {
			return true;
		}
	}
	return false;
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
}

Point2i DisplayServerOpenHarmony::mouse_get_position() const {
	return Input::get_singleton()->get_mouse_position();
}

BitField<MouseButtonMask> DisplayServerOpenHarmony::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

void DisplayServerOpenHarmony::cursor_set_shape(DisplayServerEnums::CursorShape p_shape) {
	input_manager->cursor_set_shape(p_shape);
}

DisplayServerEnums::CursorShape DisplayServerOpenHarmony::cursor_get_shape() const {
	return input_manager->cursor_get_shape();
}

void DisplayServerOpenHarmony::cursor_set_custom_image(const Ref<Resource> &p_cursor, DisplayServerEnums::CursorShape p_shape, const Vector2 &p_hotspot) {
	input_manager->cursor_set_custom_image(p_cursor);
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
	input_manager->window_set_ime_active(p_active);
}

void DisplayServerOpenHarmony::window_set_ime_position(const Point2i &p_pos, DisplayServerEnums::WindowID p_window) {
	input_manager->window_set_ime_position(p_pos);
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
	return input_manager->clipboard_has(PASTEBOARD_MIMETYPE_TEXT_PLAIN);
}

bool DisplayServerOpenHarmony::clipboard_has_image() const {
	return input_manager->clipboard_has(PASTEBOARD_MIMETYPE_PIXELMAP);
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

	OpenHarmony::WindowManager_WindowProperties properties;
	int32_t status = OpenHarmony::OH_WindowManager_GetWindowProperties(wd.native_window_id, &properties);

	if (status != OpenHarmony::OK) {
		ERR_PRINT(vformat("Failed to get window properties, error: %d.", status));
	}

	wd.is_keep_screen_on = properties.isKeepScreenOn;
	wd.display_id = properties.displayId;

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

#ifdef GLES3_ENABLED
		if (egl_manager) {
			Error err = egl_manager->window_create(p_window, nullptr, wd.native_window, wd.rect.size.width, wd.rect.size.height);
			ERR_FAIL_COND_MSG(err == ERR_CANT_CREATE, "Can't show a GLES3 window.");

			window_set_vsync_mode(wd.vsync_mode, p_window);
		}
#endif // GLES3_ENABLED

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

#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->window_destroy(p_window);
	}
#endif // GLES3_ENABLED

	windows.erase(p_window);
}

DisplayServerEnums::WindowID DisplayServerOpenHarmony::get_window_at_screen_position(const Point2i &p_position) const {
	return DisplayServerEnums::MAIN_WINDOW_ID;
}

int64_t DisplayServerOpenHarmony::window_get_native_handle(DisplayServerEnums::HandleType p_handle_type, DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(p_window != DisplayServerEnums::MAIN_WINDOW_ID, 0);
	switch (p_handle_type) {
		case DisplayServerEnums::WINDOW_HANDLE: {
			ERR_FAIL_COND_V_MSG(!windows.has(p_window), 0, vformat("The window %d does exist.", p_window));
			return windows[p_window].native_window_id;
		}
		case DisplayServerEnums::WINDOW_VIEW: {
			return 0; // Not supported.
		}
		case DisplayServerEnums::DISPLAY_HANDLE: {
			// @todo Find a way to get native screen id.
			return 0;
		}
#ifdef GLES3_ENABLED
		case DisplayServerEnums::OPENGL_CONTEXT: {
			if (egl_manager) {
				return (int64_t)egl_manager->get_context(p_window);
			}
			return 0;
		}
		case DisplayServerEnums::EGL_DISPLAY: {
			if (egl_manager) {
				return (int64_t)egl_manager->get_display(p_window);
			}
			return 0;
		}
		case DisplayServerEnums::EGL_CONFIG: {
			if (egl_manager) {
				return (int64_t)egl_manager->get_config(p_window);
			}
			return 0;
		}
#endif // GLES3_ENABLED
		default: {
			return 0;
		}
	}
}

void DisplayServerOpenHarmony::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	window_attached_instance_id = p_instance;
}

ObjectID DisplayServerOpenHarmony::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	return window_attached_instance_id;
}

void DisplayServerOpenHarmony::gl_window_make_current(DisplayServerEnums::WindowID p_window_id) {
#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->window_make_current(p_window_id);
	}
#endif // GLES3_ENABLED
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
#ifdef RD_ENABLED
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif // RD_ENABLED

#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->set_use_vsync(p_vsync_mode != DisplayServerEnums::VSYNC_DISABLED);
	}
#endif // GLES3_ENABLED
}

DisplayServerEnums::VSyncMode DisplayServerOpenHarmony::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
#ifdef RD_ENABLED
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
#endif // RD_ENABLED

#ifdef GLES3_ENABLED
	if (egl_manager) {
		return egl_manager->is_using_vsync() ? DisplayServerEnums::VSYNC_ENABLED : DisplayServerEnums::VSYNC_DISABLED;
	}
#endif // GLES3_ENABLED

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
#endif // RD_ENABLED
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
#endif // RD_ENABLED
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
#endif // RD_ENABLED
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

void DisplayServerOpenHarmony::release_rendering_thread() {
#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->release_current();
	}
#endif // GLES3_ENABLED
}

void DisplayServerOpenHarmony::swap_buffers() {
#ifdef GLES3_ENABLED
	if (egl_manager) {
		egl_manager->swap_buffers();
	}
#endif // GLES3_ENABLED
}
