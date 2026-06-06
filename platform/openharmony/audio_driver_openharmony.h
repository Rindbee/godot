/**************************************************************************/
/*  audio_driver_openharmony.h                                            */
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

#include "core/os/mutex.h"
#include "servers/audio/audio_server.h"

typedef struct OH_AudioStreamBuilderStruct OH_AudioStreamBuilder;
typedef struct OH_AudioRendererStruct OH_AudioRenderer;
typedef struct OH_AudioCapturerStruct OH_AudioCapturer;
struct OH_AudioDeviceDescriptorArray;

class AudioDriverOpenHarmony : public AudioDriver {
	int mix_rate = 0;
	int target_latency_ms = 0;
	int buffer_frames = 0;
	int32_t buffer_samples = 0;

	bool active = false;
	Mutex mutex;
	std::atomic<bool> pause{ false };

	int32_t *mixdown_buffer = nullptr;

	OH_AudioStreamBuilder *audio_stream_builder = nullptr;
	OH_AudioRenderer *audio_renderer = nullptr;

	int32_t _write_renderer_data(OH_AudioRenderer *p_renderer, void *p_audio_data, int32_t p_audio_data_size);
	static int32_t _renderer_write_data_callback(OH_AudioRenderer *p_renderer, void *p_user_data, void *p_audio_data, int32_t p_audio_data_size);

	OH_AudioStreamBuilder *audio_stream_capture_builder = nullptr;
	OH_AudioCapturer *audio_capturer = nullptr;

	// Capturer callback functions.
	int32_t _capturer_read_data(OH_AudioCapturer *p_capturer, void *p_buffer, int32_t p_length);

	static void _capturer_read_data_callback(OH_AudioCapturer *p_capturer, void *p_user_data, void *p_buffer, int32_t p_length);
	static void _capturer_device_change_callback(OH_AudioCapturer *p_capturer, void *p_user_data, OH_AudioDeviceDescriptorArray *p_device_array);

public:
	virtual const char *get_name() const override { return "OpenHarmony"; }

	virtual Error init() override;
	virtual void start() override;
	virtual int get_mix_rate() const override;
	virtual SpeakerMode get_speaker_mode() const override;

	virtual void lock() override;
	virtual void unlock() override;
	virtual void finish() override;

	virtual Error input_start() override;
	virtual Error input_stop() override;

	void set_pause(bool p_pause);

	AudioDriverOpenHarmony();
	~AudioDriverOpenHarmony();
};
