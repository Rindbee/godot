/**************************************************************************/
/*  screen_manager_openharmony.h                                          */
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

class ScreenManagerOpenharmony {
public:
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

private:
	static ScreenManagerOpenharmony *singleton;

	HashMap<uint64_t, ScreenInfo> screens;
	Vector<uint64_t> screen_ids;

	uint32_t screen_add_listener_idx = 0;
	uint32_t screen_change_listener_idx = 0;
	uint32_t screen_remove_listener_idx = 0;
	uint32_t available_area_change_listener_idx = 0;
	uint32_t fold_display_mode_change_listener_idx = 0;

	bool is_foldable = false;
	FoldDisplayMode fold_display_mode = FoldDisplayMode::FOLD_DISPLAY_MODE_UNKNOWN;

	void _update_extra_info(ScreenInfo &p_screen_info);
	void _update_all_screen_info();

	static void _display_add_or_change_callback(uint64_t p_display_id);
	static void _display_remove_callback(uint64_t p_display_id);
	static void _available_area_change_callback(uint64_t p_display_id);

public:
	void set_fold_display_mode(FoldDisplayMode p_mode);
	FoldDisplayMode get_fold_display_mode() const;

	TypedArray<Rect2> get_display_cutouts(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const;
	// Rect2i get_display_safe_area() const;

	int get_screen_count() const;
	int get_primary_screen() const;
	int get_keyboard_focus_screen() const;
	int get_screen_from_rect(const Rect2 &p_rect) const;
	Point2i screen_get_position(int p_screen) const;
	Size2i screen_get_size(int p_screen) const;
	Rect2i screen_get_usable_rect(int p_screen) const;
	int screen_get_dpi(int p_screen) const;
	float screen_get_scale(int p_screen) const;
	float screen_get_refresh_rate(int p_screen) const;
	Color screen_get_pixel(const Point2i &p_position) const;
	Ref<Image> screen_get_image(int p_screen) const;
	Ref<Image> screen_get_image_rect(const Rect2i &p_rect) const;
	bool is_touchscreen_available() const;

	void screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen);
	DisplayServerEnums::ScreenOrientation screen_get_orientation(int p_screen) const;

	void screen_set_keep_on(bool p_enable);
	bool screen_is_kept_on() const;

	Vector2 vp_to_px(Vector2 p_vp, int p_screen);

	static ScreenManagerOpenharmony *get_singleton() { return singleton; }

	ScreenManagerOpenharmony();
	~ScreenManagerOpenharmony();
};
