/**************************************************************************/
/*  pixelmap_driver.cpp                                                   */
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

#include "pixelmap_driver.h"

#include <multimedia/image_framework/image/pixelmap_native.h>

namespace PixelmapDriver {

Image::Format format_map_pixelmap_to_image(int32_t p_pixel_format) {
	switch (p_pixel_format) {
		case PIXEL_FORMAT_RGB_888:
			return Image::FORMAT_RGB8;
		case PIXEL_FORMAT_RGBA_8888:
			return Image::FORMAT_RGBA8;
		case PIXEL_FORMAT_RGB_565:
			return Image::FORMAT_RGB565;
		case PIXEL_FORMAT_RGBA_F16:
			return Image::FORMAT_RGBAH;
		default:
			return Image::FORMAT_MAX;
	}
}

int32_t format_map_image_to_pixelmap(Image::Format p_format) {
	switch (p_format) {
		case Image::FORMAT_RGB8:
			return PIXEL_FORMAT_RGB_888;
		case Image::FORMAT_RGBA8:
			return PIXEL_FORMAT_RGBA_8888;
		case Image::FORMAT_RGB565:
			return PIXEL_FORMAT_RGB_565;
		case Image::FORMAT_RGBAH:
			return PIXEL_FORMAT_RGBA_F16;
		default:
			return PIXEL_FORMAT_UNKNOWN;
	}
}

int32_t get_pixel_bytes(int32_t p_pixel_format) {
	switch (p_pixel_format) {
		case PIXEL_FORMAT_RGB_565:
			return 2;
		case PIXEL_FORMAT_RGBA_8888:
		case PIXEL_FORMAT_BGRA_8888:
			return 4;
		case PIXEL_FORMAT_RGB_888:
			return 3;
		case PIXEL_FORMAT_ALPHA_8:
			return 1;
		case PIXEL_FORMAT_RGBA_F16:
			return 8;
		case PIXEL_FORMAT_NV21:
		case PIXEL_FORMAT_NV12:
			return 2;
		case PIXEL_FORMAT_RGBA_1010102:
			return 4;
		case PIXEL_FORMAT_YCBCR_P010:
		case PIXEL_FORMAT_YCRCB_P010:
			return 2;
		default:
			return 0;
	}
}

Error pixelmap_get_data_rect(OH_PixelmapNative *p_pixelmap, const Rect2i p_rect, Vector<uint8_t> &r_data) {
	ERR_FAIL_NULL_V(p_pixelmap, ERR_INVALID_PARAMETER);

	Image_PositionArea area;
	area.pixels = r_data.ptrw();
	area.pixelsSize = r_data.size();
	area.offset = 0;
	area.stride = p_rect.size.width * 4; // Make sure the read data is compact.
	area.region.x = p_rect.position.x;
	area.region.y = p_rect.position.y;
	area.region.width = p_rect.size.width;
	area.region.height = p_rect.size.height;

	Image_ErrorCode pixel_err = OH_PixelmapNative_ReadPixelsFromArea(p_pixelmap, &area);
	ERR_FAIL_COND_V_MSG(pixel_err != IMAGE_SUCCESS, ERR_INVALID_DATA, vformat("Failed to read pixels from rect %s.", p_rect));
	return OK;
}

Color pixelmap_get_color(OH_PixelmapNative *p_pixelmap, const Point2i &p_position, Error &r_error) {
	Vector<uint8_t> pixel_data;
	pixel_data.resize(4);
	r_error = pixelmap_get_data_rect(p_pixelmap, Rect2i(p_position, Size2i(1, 1)), pixel_data);
	if (r_error != OK) {
		return Color();
	}
	return Color::from_rgba8(pixel_data[2], pixel_data[1], pixel_data[0], pixel_data[3]);
}

Error pixelmap_get_image_rect(OH_PixelmapNative *p_pixelmap, const Rect2i p_rect, Ref<Image> &r_image) {
	ERR_FAIL_NULL_V(p_pixelmap, ERR_INVALID_PARAMETER);

	const uint32_t pixel_count = p_rect.size.width * p_rect.size.height;

	Vector<uint8_t> image_data;
	image_data.resize(pixel_count * 4);

	Error error = pixelmap_get_data_rect(p_pixelmap, p_rect, image_data);
	if (error != OK) {
		return error;
	}

	uint8_t *image_ptrw = image_data.ptrw();
	for (uint32_t i = 0; i < pixel_count; i++) {
		SWAP(image_ptrw[i * 4 + 0], image_ptrw[i * 4 + 2]); // Swap B and R.
	}

	r_image = Image::create_from_data(p_rect.size.width, p_rect.size.height, false, Image::FORMAT_RGBA8, image_data);
	return OK;
}

Error pixelmap_to_image(OH_PixelmapNative *p_pixelmap, Ref<Image> &r_image) {
	ERR_FAIL_NULL_V(p_pixelmap, ERR_INVALID_PARAMETER);

	// Fetch the info of the pixelmap.

	OH_Pixelmap_ImageInfo *image_info = nullptr;
	OH_PixelmapImageInfo_Create(&image_info);
	ERR_FAIL_NULL_V(image_info, ERR_CANT_CREATE);

	Image_ErrorCode err = OH_PixelmapNative_GetImageInfo(p_pixelmap, image_info);
	if (err != IMAGE_SUCCESS) {
		OH_PixelmapImageInfo_Release(image_info);
		ERR_PRINT(vformat("Failed to read image info, its inner pixelmap may be null."));
		return ERR_INVALID_DATA;
	}

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t row_stride = 0;
	int32_t pixel_format = PIXEL_FORMAT_UNKNOWN;

	OH_PixelmapImageInfo_GetWidth(image_info, &width);
	OH_PixelmapImageInfo_GetHeight(image_info, &height);
	OH_PixelmapImageInfo_GetRowStride(image_info, &row_stride);
	OH_PixelmapImageInfo_GetPixelFormat(image_info, &pixel_format);
	OH_PixelmapImageInfo_Release(image_info);

	uint32_t byte_count = 0;

	OH_PixelmapNative_GetByteCount(p_pixelmap, &byte_count);
	ERR_FAIL_COND_V_MSG((byte_count / 4) != (width * height), ERR_INVALID_DATA, "Failed to fetch pixelmap info.");

	Image::Format format = format_map_pixelmap_to_image(pixel_format);
	if (format != Image::FORMAT_MAX && row_stride == (width * get_pixel_bytes(pixel_format))) {
		// The format is supported and the data is compact.
		Vector<uint8_t> source_data;
		source_data.resize(byte_count);
		size_t buffer_size = byte_count;
		err = OH_PixelmapNative_ReadPixels(p_pixelmap, source_data.ptrw(), &buffer_size);
		ERR_FAIL_COND_V_MSG(err != IMAGE_SUCCESS, ERR_INVALID_DATA, vformat("Failed to read data from the pixelmap with Image_ErrorCode: %d.", err));
		ERR_FAIL_COND_V(buffer_size == byte_count, ERR_INVALID_DATA);

		r_image = Image::create_from_data(width, height, false, format, source_data);
		return OK;
	}

	return pixelmap_get_image_rect(p_pixelmap, Rect2i(0, 0, width, height), r_image);
}

Error image_to_pixelmap(const Ref<Image> &p_image, OH_PixelmapNative *r_pixelmap) {
	ERR_FAIL_COND_V(p_image.is_null(), ERR_INVALID_PARAMETER);

	OH_Pixelmap_InitializationOptions *options = nullptr;
	OH_PixelmapInitializationOptions_Create(&options);
	ERR_FAIL_NULL_V(options, ERR_CANT_CREATE);

	if (p_image->is_compressed()) {
		p_image->decompress();
	}
	int32_t pixel_format = format_map_image_to_pixelmap(p_image->get_format());
	if (pixel_format == PIXEL_FORMAT_UNKNOWN) {
		p_image->convert(Image::FORMAT_RGBA8);
		pixel_format = PIXEL_FORMAT_RGBA_8888;
	}

	const int32_t row_stride = p_image->get_width() * get_pixel_bytes(pixel_format);
	const int32_t alpha_mode = (p_image->detect_alpha() == Image::ALPHA_NONE ? PIXELMAP_ALPHA_TYPE_OPAQUE : PIXELMAP_ALPHA_TYPE_PREMULTIPLIED);

	OH_PixelmapInitializationOptions_SetSrcPixelFormat(options, pixel_format);
	OH_PixelmapInitializationOptions_SetPixelFormat(options, pixel_format);
	OH_PixelmapInitializationOptions_SetWidth(options, p_image->get_width());
	OH_PixelmapInitializationOptions_SetHeight(options, p_image->get_height());
	OH_PixelmapInitializationOptions_SetRowStride(options, row_stride);
	OH_PixelmapInitializationOptions_SetAlphaType(options, alpha_mode);

	Image_ErrorCode err = OH_PixelmapNative_CreatePixelmap(p_image->ptrw(), p_image->get_data_size(), options, &r_pixelmap);
	OH_PixelmapInitializationOptions_Release(options);
	options = nullptr;
	ERR_FAIL_COND_V_MSG(err != IMAGE_SUCCESS, ERR_CANT_CREATE, vformat("Failed to create pixelmap Image_ErrorCode: %d.", err));

	return OK;
}

}; // namespace PixelmapDriver
