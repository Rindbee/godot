/**************************************************************************/
/*  file_watcher_iocp.h                                                   */
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

#ifdef WINDOWS_ENABLED

#include "core/io/file_watcher.h"

#include <windows.h>

class FileWatcherIOCP : public FileWatcher {
	GDSOFTCLASS(FileWatcherIOCP, FileWatcher);

private:
	static constexpr ULONG_PTR IOCP_SHUTDOWN_KEY = 0xFFFFFFFF;

	HANDLE iocp_handle = nullptr;

	uint32_t last_watch_id = 0;

	struct DirWatchIOCP : DirWatch {
		OVERLAPPED overlapped = {};
		HANDLE handle = INVALID_HANDLE_VALUE;
		Vector<uint8_t> buffer;
		DWORD notify_filter = 0;
		SafeFlag stop_now;
		bool extended = false;
		FileWatcherIOCP *watcher = nullptr;
	};

	struct PendingEvent {
		WatchID id = UNAVAILABLE_WATCHER;
		DirWatchIOCP *watch = nullptr;
		String dir_path;
		String filename;
		LONGLONG file_id = -1;
		BitField<FileListener::FileTypeFlags> type = 0;
		uint64_t timestamp_ms = 0;
	};
	static constexpr uint64_t MATCH_TIMEOUT_MS = 500;
	HashMap<LONGLONG, PendingEvent> moves;
	HashMap<LONGLONG, PendingEvent> deletions;

	struct EventInfo {
		DWORD offset = 0;
		bool has_next = true;
		bool skip_current = false;
		String dir_path;
		String filename;
		BitField<FileListener::FileTypeFlags> type = 0;
		LONGLONG file_id = -1;
		DWORD action = 0;
	};

	void _clear_pending_events(DirWatchIOCP *p_for_watch, bool p_is_deletion);
	void _cancel_pending_deletion(const String &p_dir_path, const String &p_filename, LONGLONG p_file_id = -1);

	void _update_info(EventInfo &event, DWORD p_offset, const String &p_scope, const String &p_file_path, DWORD p_action, DWORD p_file_attr, DWORD p_reparse_point_tag = 0, LONGLONG p_file_id = -1);
	void _update_info_from_buffer(const DirWatchIOCP *p_watch, EventInfo &event);
	void _process_event(DirWatchIOCP *p_watch, const EventInfo &p_event);
	void _process_buffered_events(DirWatchIOCP *p_watch, DWORD p_total_bytes);

	virtual bool _run() override;
	virtual void _stop_watch_internally() override;

	bool _refresh_watch(DirWatchIOCP *p_watch, bool p_clean_if_failed);
	virtual WatchID _create_watch(const String &p_dir_path, bool p_recursive, DirWatch **r_watch) override;

	void _destroy_watch(DirWatchIOCP *p_watch);
	virtual void _remove_watch(WatchID p_id) override;
	void _clear_watches();

	typedef BOOL(WINAPI *ReadDirectoryChangesExWPtr)(HANDLE hDirectory, LPVOID lpBuffer, DWORD nBufferLength,
			BOOL bWatchSubtree, DWORD dwNotifyFilter, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped,
			LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, DWORD ReadDirectoryNotifyInformationClass);
	static inline ReadDirectoryChangesExWPtr w10_ReadDirectoryChangesExW = nullptr;
	static void _init_win_api();

protected:
	static Ref<FileWatcher> _create_func();

public:
	static void make_default();

	FileWatcherIOCP();
	~FileWatcherIOCP() override;
};

#endif // WINDOWS_ENABLED
