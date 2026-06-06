/**************************************************************************/
/*  screen_manager_openharmony.cpp                                        */
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

#include "screen_manager_openharmony.h"

#include "pixelmap_driver.h"

#include "core/os/os.h"
#include "core/variant/typed_array.h"

#include <window_manager/oh_display_capture.h>
#include <window_manager/oh_display_manager.h>

ScreenManagerOpenharmony *ScreenManagerOpenharmony::singleton = nullptr;

static void _update_display_info_from(ScreenManagerOpenharmony::ScreenInfo &p_screen, NativeDisplayManager_DisplayInfo *p_native_display) {
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
	p_screen.rotation = (ScreenManagerOpenharmony::ScreenRotation)p_native_display->rotation;
	p_screen.state = (ScreenManagerOpenharmony::ScreenState)p_native_display->state;
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

void ScreenManagerOpenharmony::_update_extra_info(ScreenInfo &p_screen_info) {
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

void ScreenManagerOpenharmony::_update_all_screen_info() {
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

void ScreenManagerOpenharmony::_display_add_or_change_callback(uint64_t p_display_id) {
	NativeDisplayManager_DisplayInfo *native_display = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateDisplayById(p_display_id, &native_display);
	ERR_FAIL_COND_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to create display for id: %d, error: %d.", p_display_id, err));

	if (!singleton->screens.has(p_display_id)) {
		singleton->screen_ids.push_back(p_display_id);
	}

	ScreenInfo &screen = singleton->screens[p_display_id];
	_update_display_info_from(screen, native_display);
	singleton->_update_extra_info(screen);

	OH_NativeDisplayManager_DestroyDisplay(native_display);
}

void ScreenManagerOpenharmony::_display_remove_callback(uint64_t p_display_id) {
	HashMap<uint64_t, ScreenInfo>::Iterator I = singleton->screens.find(p_display_id);
	ERR_FAIL_NULL_MSG(I, vformat("The display %d wasn't found.", p_display_id));
	singleton->screens.remove(I);
	singleton->screen_ids.erase(p_display_id);
}

void ScreenManagerOpenharmony::_available_area_change_callback(uint64_t p_display_id) {
	HashMap<uint64_t, ScreenInfo>::Iterator I = singleton->screens.find(p_display_id);
	ERR_FAIL_NULL_MSG(I, vformat("The display %d wasn't found.", p_display_id));

	NativeDisplayManager_Rect *rect = nullptr;
	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_CreateAvailableArea(p_display_id, &rect);
	ERR_FAIL_COND_MSG(err != DISPLAY_MANAGER_OK, vformat("Failed to create available area for id: %d, error: %d.", p_display_id, err));
	I->value.available_rect = Rect2i(rect->left, rect->top, rect->width, rect->height);
	OH_NativeDisplayManager_DestroyAvailableArea(rect);
}

static void _fold_display_mode_change_callback(NativeDisplayManager_FoldDisplayMode displayMode) {
	ScreenManagerOpenharmony::get_singleton()->set_fold_display_mode((ScreenManagerOpenharmony::FoldDisplayMode)displayMode);
}

void ScreenManagerOpenharmony::set_fold_display_mode(FoldDisplayMode p_mode) {
	fold_display_mode = p_mode;
}

ScreenManagerOpenharmony::FoldDisplayMode ScreenManagerOpenharmony::get_fold_display_mode() const {
	return fold_display_mode;
}

TypedArray<Rect2> ScreenManagerOpenharmony::get_display_cutouts(int p_screen) const {
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

int ScreenManagerOpenharmony::get_screen_count() const {
	return screen_ids.size();
}

int ScreenManagerOpenharmony::get_primary_screen() const {
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

int ScreenManagerOpenharmony::get_keyboard_focus_screen() const {
	return get_primary_screen();
}

int ScreenManagerOpenharmony::get_screen_from_rect(const Rect2 &p_rect) const {
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

Point2i ScreenManagerOpenharmony::screen_get_position(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Point2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Point2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.global_position;
}

Size2i ScreenManagerOpenharmony::screen_get_size(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Size2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Size2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.size;
}

Rect2i ScreenManagerOpenharmony::screen_get_usable_rect(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Rect2i());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Rect2i(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.available_rect;
}

int ScreenManagerOpenharmony::screen_get_dpi(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), 0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, 0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.density_dpi;
}

float ScreenManagerOpenharmony::screen_get_scale(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), -1.0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, -1.0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.scaled_density;
}

float ScreenManagerOpenharmony::screen_get_refresh_rate(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), -1.0);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, -1.0, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.refresh_rate;
}

Color ScreenManagerOpenharmony::screen_get_pixel(const Point2i &p_position) const {
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

Ref<Image> ScreenManagerOpenharmony::screen_get_image(int p_screen) const {
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

Ref<Image> ScreenManagerOpenharmony::screen_get_image_rect(const Rect2i &p_rect) const {
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

bool ScreenManagerOpenharmony::is_touchscreen_available() const {
	return true;
}

void ScreenManagerOpenharmony::screen_set_orientation(DisplayServerEnums::ScreenOrientation p_orientation, int p_screen) {
	// Not supported on OpenHarmony.
}

DisplayServerEnums::ScreenOrientation ScreenManagerOpenharmony::screen_get_orientation(int p_screen) const {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), DisplayServerEnums::SCREEN_PORTRAIT);
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, DisplayServerEnums::SCREEN_PORTRAIT, vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return I->value.orientation;
}

Vector2 ScreenManagerOpenharmony::vp_to_px(Vector2 p_vp, int p_screen) {
	ERR_FAIL_INDEX_V(p_screen, screen_ids.size(), Vector2());
	uint64_t display_id = screen_ids[p_screen];
	HashMap<uint64_t, ScreenInfo>::ConstIterator I = screens.find(display_id);
	ERR_FAIL_NULL_V_MSG(I, Vector2(), vformat("The screen %d (display id %d) doesn't find.", p_screen, display_id));
	return p_vp * I->value.density_pixels;
}

ScreenManagerOpenharmony::ScreenManagerOpenharmony() {
	singleton = this;

	NativeDisplayManager_ErrorCode err = OH_NativeDisplayManager_RegisterDisplayAddListener(_display_add_or_change_callback, &screen_add_listener_idx);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to register display add listener, error: %d.", err));
	}

	err = OH_NativeDisplayManager_RegisterDisplayChangeListener(_display_add_or_change_callback, &screen_change_listener_idx);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to register display change listener, error: %d.", err));
	}

	err = OH_NativeDisplayManager_RegisterDisplayRemoveListener(_display_remove_callback, &screen_remove_listener_idx);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to register display remove listener, error: %d.", err));
	}

	err = OH_NativeDisplayManager_RegisterAvailableAreaChangeListener(_available_area_change_callback, &available_area_change_listener_idx);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to register available area change listener, error: %d.", err));
	}

	is_foldable = OH_NativeDisplayManager_IsFoldable();

	if (is_foldable) {
		err = OH_NativeDisplayManager_RegisterFoldDisplayModeChangeListener(_fold_display_mode_change_callback, &fold_display_mode_change_listener_idx);
		if (err != DISPLAY_MANAGER_OK) {
			ERR_PRINT(vformat("Failed to register fold display mode change listener, error: %d.", err));
		}

		NativeDisplayManager_FoldDisplayMode native_fold_display_mode;
		OH_NativeDisplayManager_GetFoldDisplayMode(&native_fold_display_mode);
		fold_display_mode = (FoldDisplayMode)native_fold_display_mode;
	}

	_update_all_screen_info();
}

ScreenManagerOpenharmony::~ScreenManagerOpenharmony() {
	singleton = nullptr;

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

	err = OH_NativeDisplayManager_UnregisterFoldDisplayModeChangeListener(fold_display_mode_change_listener_idx);
	if (err != DISPLAY_MANAGER_OK) {
		ERR_PRINT(vformat("Failed to unregister fold display mode change listener, error: %d.", err));
	}
}
