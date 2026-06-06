/**************************************************************************/
/*  napi_bridge.h                                                         */
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

struct napi_env__;
struct napi_value__;
struct napi_callback_info__;

struct ArkUI_Node;
struct ArkUI_NodeContent;
struct ArkUI_NodeContentEvent;

struct OH_ArkUI_SurfaceHolder;
struct OH_ArkUI_SurfaceCallback;

class OS_OpenHarmony;

struct NodeData {
	int window_id = -1;
	int native_window_id = -1;
	ArkUI_NodeContent *parent = nullptr;
	ArkUI_Node *node = nullptr;
	OH_ArkUI_SurfaceHolder *holder = nullptr;
	OH_ArkUI_SurfaceCallback *callback = nullptr;

	~NodeData();
};

class NAPIBridge {
private:
	static NAPIBridge singleton;

	OS_OpenHarmony *os = nullptr;

	bool started = false;
	bool initialized = false;

	void _stop();

	static napi_value__ *_bool_to_value(napi_env__ *p_env, bool p_source);

	static void _stop_engine();

	static ArkUI_Node *_create_xc(NodeData *p_node_data);

	static void node_content_callback(ArkUI_NodeContentEvent *p_event);

public:
	static void _setup_engine(OH_ArkUI_SurfaceHolder *p_holder);

	static napi_value__ *initialize(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *finalize(napi_env__ *p_env, napi_callback_info__ *p_info);

	static napi_value__ *set_main_window_id(napi_env__ *p_env, napi_callback_info__ *p_info);

	static napi_value__ *is_started(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *iteration(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *stop(napi_env__ *p_env, napi_callback_info__ *p_info);

	static napi_value__ *focus_out(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *focus_in(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *pause(napi_env__ *p_env, napi_callback_info__ *p_info);
	static napi_value__ *resume(napi_env__ *p_env, napi_callback_info__ *p_info);

	static napi_value__ *create_native_node(napi_env__ *p_env, napi_callback_info__ *p_info);

	static NAPIBridge *get_singleton() { return &singleton; }

	void initialize_node_data(NodeData *p_node_data);
	void finalize_node_data(NodeData *p_node_data);

	bool start_ability(const char *p_project_id, const char *p_arguments) const;
	bool terminate_self_ability() const;

	NAPIBridge();
	~NAPIBridge();
};
