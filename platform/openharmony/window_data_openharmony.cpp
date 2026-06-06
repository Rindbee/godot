/**************************************************************************/
/*  window_data_openharmony.cpp                                           */
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

#include "window_data_openharmony.h"

#include <window_manager/oh_window.h>

bool WindowData::setup(int32_t p_native_window_id) {
	WindowManager_WindowProperties window_properties;
	if (OH_WindowManager_GetWindowProperties(p_native_window_id, &window_properties) != OK) {
		return false;
	}
	position[0] = window_properties.windowRect.posX;
	position[1] = window_properties.windowRect.posY;
	size[0] = window_properties.windowRect.width;
	size[1] = window_properties.windowRect.height;

	drawable_position[0] = window_properties.drawableRect.posX;
	drawable_position[1] = window_properties.drawableRect.posY;
	drawable_size[0] = window_properties.drawableRect.width;
	drawable_size[1] = window_properties.drawableRect.height;

	switch (window_properties.type) {
		case WINDOW_MANAGER_WINDOW_TYPE_APP: {
			type = TYPE_APP;
		} break;
		case WINDOW_MANAGER_WINDOW_TYPE_MAIN: {
			type = TYPE_MAIN;
		} break;
		case WINDOW_MANAGER_WINDOW_TYPE_FLOAT: {
			type = TYPE_FLOAT;
		} break;
		case WINDOW_MANAGER_WINDOW_TYPE_DIALOG: {
			type = TYPE_DIALOG;
		} break;
	}
	is_fullscreen = window_properties.isFullScreen;
	is_layout_fullscreen = window_properties.isLayoutFullScreen;
	focusable = window_properties.focusable;
	touchable = window_properties.touchable;
	brightness = window_properties.brightness;
	is_keep_screen_on = window_properties.isKeepScreenOn;
	is_privacy_mode = window_properties.isPrivacyMode;
	is_transparent = window_properties.isTransparent;
	native_window_id = window_properties.id;
	native_display_id = window_properties.displayId;

	return true;
}
