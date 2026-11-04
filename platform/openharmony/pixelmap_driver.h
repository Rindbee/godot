/**************************************************************************/
/*  pixelmap_driver.h                                                     */
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

struct OH_PixelmapNative;

namespace PixelmapDriver {

Image::Format format_map_pixelmap_to_image(int32_t p_pixel_format);
int32_t format_map_image_to_pixelmap(Image::Format p_format);
int32_t get_pixel_bytes(int32_t p_pixel_format);

Error pixelmap_get_data_rect(OH_PixelmapNative *p_pixelmap, const Rect2i p_rect, Vector<uint8_t> &r_data);
Color pixelmap_get_color(OH_PixelmapNative *p_pixelmap, const Point2i &p_position, Error &r_error);
Error pixelmap_get_image_rect(OH_PixelmapNative *p_pixelmap, const Rect2i p_rect, Ref<Image> &r_image);
Error pixelmap_to_image(OH_PixelmapNative *p_pixelmap, Ref<Image> &r_image);
Error image_to_pixelmap(const Ref<Image> &p_image, OH_PixelmapNative *r_pixelmap);

}; // namespace PixelmapDriver
