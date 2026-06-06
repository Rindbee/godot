/**************************************************************************/
/*  window_data_openharmony.h                                             */
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

#include <cstdint>

struct WindowData {
	enum WindowType {
		TYPE_APP = 0,
		TYPE_MAIN = 1,
		TYPE_FLOAT = 8,
		TYPE_DIALOG = 16,
	};

	int32_t position[2] = {};
	uint32_t size[2] = {};
	int32_t drawable_position[2] = {};
	uint32_t drawable_size[2] = {};
	WindowType type = TYPE_MAIN;
	bool is_fullscreen = false;
	bool is_layout_fullscreen = false;
	bool focusable = true;
	bool touchable = false;
	float brightness = 0.0f;
	bool is_keep_screen_on = false;
	bool is_privacy_mode = false;
	bool is_transparent = false;
	uint32_t native_window_id = 0;
	uint32_t native_display_id = 0;

	bool setup(int32_t p_native_window_id);

	WindowData();
	~WindowData();
};
