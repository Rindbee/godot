/**************************************************************************/
/*  audio_driver_openharmony.cpp                                          */
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

#include "audio_driver_openharmony.h"

#include "core/error/error_macros.h"
#include "core/math/math_funcs_binary.h"
#include "core/os/os.h"
#include "core/variant/variant.h"

#include <ohaudio/native_audiocapturer.h>
#include <ohaudio/native_audiostreambuilder.h>

#define CHANNEL_COUNT 2

Error AudioDriverOpenHarmony::init() {
	mix_rate = _get_configured_mix_rate();
	target_latency_ms = Engine::get_singleton()->get_audio_output_latency();
	buffer_frames = Math::closest_power_of_2(uint64_t(target_latency_ms * mix_rate / 1000));
	buffer_samples = buffer_frames * CHANNEL_COUNT;

	return OK;
}

int32_t AudioDriverOpenHarmony::_write_renderer_data(OH_AudioRenderer *p_renderer, void *p_audio_data, int32_t p_audio_data_size) {
	if (pause.load()) {
		memset(p_audio_data, 0, p_audio_data_size);
		return 0;
	}

	lock();
	start_counting_ticks();
	audio_server_process(buffer_frames, mixdown_buffer);

	int32_t samples_available = p_audio_data_size / sizeof(int16_t);
	int32_t samples_to_render = MIN(buffer_samples, samples_available);

	const int32_t *src_buff = mixdown_buffer;
	int16_t *ptr = static_cast<int16_t *>(p_audio_data);

	for (int32_t i = 0; i < samples_to_render; i++) {
		ptr[i] = static_cast<int16_t>(src_buff[i] >> 16);
	}

	if (samples_available > samples_to_render) {
		memset(ptr + samples_to_render, 0, (samples_available - samples_to_render) * sizeof(int16_t));
	}

	stop_counting_ticks();
	unlock();
	return samples_to_render * sizeof(int16_t);
}

int32_t AudioDriverOpenHarmony::_renderer_write_data_callback(OH_AudioRenderer *p_renderer, void *p_user_data, void *p_audio_data, int32_t p_audio_data_size) {
	AudioDriverOpenHarmony *ad = static_cast<AudioDriverOpenHarmony *>(p_user_data);
	return ad->_write_renderer_data(p_renderer, p_audio_data, p_audio_data_size);
}

static void _renderer_device_change_callback(OH_AudioRenderer *p_renderer, void *p_user_data, OH_AudioStream_DeviceChangeReason p_reason) {
	WARN_PRINT("Renderer device changed.");
}

static void _renderer_interrupt_callback(OH_AudioRenderer *p_renderer, void *p_user_data, OH_AudioInterrupt_ForceType p_type, OH_AudioInterrupt_Hint p_hint) {
	if (p_type == AUDIOSTREAM_INTERRUPT_SHARE) {
		return;
	}

	AudioDriverOpenHarmony *ad = static_cast<AudioDriverOpenHarmony *>(p_user_data);

	switch (p_hint) {
		case AUDIOSTREAM_INTERRUPT_HINT_RESUME: {
			ad->set_pause(false);
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_PAUSE:
		case AUDIOSTREAM_INTERRUPT_HINT_STOP: {
			ad->set_pause(true);
		} break;
		default: {
		} break;
	}
}

static void _renderer_error_callback(OH_AudioRenderer *p_renderer, void *p_user_data, OH_AudioStream_Result p_error) {
	if (p_error != AUDIOSTREAM_SUCCESS) {
		ERR_PRINT(vformat("Failed to render audio stream frame: %d.", p_error));
	}
}

void AudioDriverOpenHarmony::start() {
	if (active) {
		return;
	}

	if (!mixdown_buffer) {
		mixdown_buffer = memnew_arr(int32_t, buffer_samples);
	}

	OH_AudioStreamBuilder *builder = nullptr;
	OH_AudioStream_Result result = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
	ERR_FAIL_COND_MSG(result != AUDIOSTREAM_SUCCESS, vformat("Failed to create audio stream builder: %d.", result));

	OH_AudioStreamBuilder_SetSamplingRate(builder, get_mix_rate());
	OH_AudioStreamBuilder_SetChannelCount(builder, CHANNEL_COUNT);
	OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_S16LE);
	OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_MUSIC);
	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	OH_AudioStreamBuilder_SetChannelLayout(builder, CH_LAYOUT_STEREO);

	OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, buffer_frames);
	OH_AudioStreamBuilder_SetRendererWriteDataCallbackAdvanced(builder, _renderer_write_data_callback, this);
	OH_AudioStreamBuilder_SetRendererOutputDeviceChangeCallback(builder, _renderer_device_change_callback, this);
	OH_AudioStreamBuilder_SetRendererInterruptCallback(builder, _renderer_interrupt_callback, this);
	OH_AudioStreamBuilder_SetRendererErrorCallback(builder, _renderer_error_callback, this);

	OH_AudioRenderer *renderer = nullptr;
	OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer);

	audio_stream_builder = builder;
	audio_renderer = renderer;

	result = OH_AudioRenderer_Start(audio_renderer);
	if (result != AUDIOSTREAM_SUCCESS) {
		finish();
		ERR_FAIL_MSG(vformat("Failed to start audio renderer: %d.", result));
	}
	active = true;
}

int AudioDriverOpenHarmony::get_mix_rate() const {
	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverOpenHarmony::get_speaker_mode() const {
	return SPEAKER_MODE_STEREO;
}

void AudioDriverOpenHarmony::lock() {
	mutex.lock();
}

void AudioDriverOpenHarmony::unlock() {
	mutex.unlock();
}

void AudioDriverOpenHarmony::finish() {
	if (audio_renderer) {
		OH_AudioRenderer_Stop(audio_renderer);
		OH_AudioRenderer_Flush(audio_renderer);
		OH_AudioRenderer_Release(audio_renderer);
		audio_renderer = nullptr;
	}

	if (audio_stream_builder) {
		OH_AudioStreamBuilder_Destroy(audio_stream_builder);
		audio_stream_builder = nullptr;
	}

	if (mixdown_buffer) {
		memdelete_arr(mixdown_buffer);
		mixdown_buffer = nullptr;
	}
	active = false;
}

int32_t AudioDriverOpenHarmony::_capturer_read_data(OH_AudioCapturer *p_capturer, void *p_buffer, int32_t p_length) {
	int16_t *input_data = static_cast<int16_t *>(p_buffer);
	int32_t samples = p_length / sizeof(int16_t);

	for (int32_t i = 0; i < samples; i += 2) {
		int32_t left_sample = input_data[i] << 16;
		int32_t right_sample = (i + 1 < samples) ? input_data[i + 1] << 16 : left_sample;

		input_buffer_write(left_sample);
		input_buffer_write(right_sample);
	}

	return p_length;
}

void AudioDriverOpenHarmony::_capturer_read_data_callback(OH_AudioCapturer *p_capturer, void *p_user_data, void *p_buffer, int32_t p_length) {
	AudioDriverOpenHarmony *ad = static_cast<AudioDriverOpenHarmony *>(p_user_data);
	ad->_capturer_read_data(p_capturer, p_buffer, p_length);
}

void AudioDriverOpenHarmony::_capturer_device_change_callback(OH_AudioCapturer *p_capturer, void *p_user_data, OH_AudioDeviceDescriptorArray *p_device_array) {
	WARN_PRINT("Capturer device changed.");
}

static void _capturer_interrupt_callback(OH_AudioCapturer *p_capturer, void *p_user_data, OH_AudioInterrupt_ForceType p_type, OH_AudioInterrupt_Hint p_hint) {
	if (p_type == AUDIOSTREAM_INTERRUPT_SHARE) {
		return;
	}

	switch (p_hint) {
		case AUDIOSTREAM_INTERRUPT_HINT_NONE: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_RESUME: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_PAUSE: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_STOP: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_DUCK: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_UNDUCK: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_MUTE: {
		} break;
		case AUDIOSTREAM_INTERRUPT_HINT_UNMUTE: {
		} break;
	}
}

static void _capturer_error_callback(OH_AudioCapturer *p_capturer, void *p_user_data, OH_AudioStream_Result p_error) {
	if (p_error != AUDIOSTREAM_SUCCESS) {
		ERR_PRINT(vformat("Failed to capturer audio stream frame: %d.", p_error));
	}
}

Error AudioDriverOpenHarmony::input_start() {
	if (!OS::get_singleton()->request_permission("ohos.permission.MICROPHONE")) {
		ERR_FAIL_V_MSG(FAILED, "Microphone permission not granted.");
	}

	if (audio_capturer) {
		return OK;
	}

	OH_AudioStreamBuilder *builder = nullptr;
	OH_AudioStream_Result result = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_CAPTURER);
	ERR_FAIL_COND_V_MSG(result != AUDIOSTREAM_SUCCESS, FAILED, vformat("Failed to create audio capture stream builder: %d.", result));

	OH_AudioStreamBuilder_SetSamplingRate(builder, get_mix_rate());
	OH_AudioStreamBuilder_SetChannelCount(builder, CHANNEL_COUNT);
	OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_S16LE);
	OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	OH_AudioStreamBuilder_SetCapturerInfo(builder, AUDIOSTREAM_SOURCE_TYPE_MIC);
	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, buffer_frames);

	OH_AudioStreamBuilder_SetCapturerReadDataCallback(builder, _capturer_read_data_callback, this);
	OH_AudioStreamBuilder_SetCapturerDeviceChangeCallback(builder, _capturer_device_change_callback, this);
	OH_AudioStreamBuilder_SetCapturerInterruptCallback(builder, _capturer_interrupt_callback, this);
	OH_AudioStreamBuilder_SetCapturerErrorCallback(builder, _capturer_error_callback, this);

	OH_AudioCapturer *capturer = nullptr;
	OH_AudioStreamBuilder_GenerateCapturer(builder, &capturer);

	audio_stream_capture_builder = builder;
	audio_capturer = capturer;

	input_buffer_init(buffer_samples);

	result = OH_AudioCapturer_Start(audio_capturer);
	if (result != AUDIOSTREAM_SUCCESS) {
		input_stop();
		ERR_FAIL_V_MSG(FAILED, vformat("Failed to start capturer: %d.", result));
	}
	return OK;
}

Error AudioDriverOpenHarmony::input_stop() {
	if (audio_capturer) {
		OH_AudioCapturer_Stop(audio_capturer);
		OH_AudioCapturer_Flush(audio_capturer);
		OH_AudioCapturer_Release(audio_capturer);
		audio_capturer = nullptr;
	}

	if (audio_stream_capture_builder) {
		OH_AudioStreamBuilder_Destroy(audio_stream_capture_builder);
		audio_stream_capture_builder = nullptr;
	}
	return OK;
}

void AudioDriverOpenHarmony::set_pause(bool p_pause) {
	if (pause.load() == p_pause) {
		return;
	}
	pause.store(p_pause);

	if (active && audio_renderer) {
		if (p_pause) {
			OH_AudioRenderer_Pause(audio_renderer);
		} else {
			OH_AudioRenderer_Start(audio_renderer);
		}
	}
}

AudioDriverOpenHarmony::AudioDriverOpenHarmony() {}

AudioDriverOpenHarmony::~AudioDriverOpenHarmony() {
	finish();
	input_stop();
}
