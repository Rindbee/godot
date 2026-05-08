/**************************************************************************/
/*  file_watcher_inotify.h                                                */
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

#ifdef __linux__

#include "core/io/file_watcher.h"

class FileWatcherInotify : public FileWatcher {
	GDSOFTCLASS(FileWatcherInotify, FileWatcher);

private:
	int inotify_fd = -1;

	char *buffer = nullptr;

	struct DirWatchInotify : DirWatch {
		DirWatchInotify *parent = nullptr;
		WatchID wd = UNAVAILABLE_WATCHER;
		Vector<DirWatchInotify *> children;
	};

	HashMap<WatchID, DirWatchInotify *> all_watches;

	struct {
		bool modified = false;
		WatchID wd = UNAVAILABLE_WATCHER;
		String name;
	} prev_event;

	struct PendingMoveEvent {
		WatchID wd = UNAVAILABLE_WATCHER;
		DirWatchInotify *watch = nullptr;
		String filename;
		BitField<FileListener::FileTypeFlags> type = 0;
		uint64_t timestamp_ms = 0;
	};
	static constexpr uint64_t MATCH_TIMEOUT_MS = 500;
	HashMap<uint32_t, PendingMoveEvent> moves;

	void _clear_move_events(DirWatchInotify *p_for_watch);
	void _clear_pending_events();

	void _overflow();
	void _check_new_watch_for(DirWatchInotify *p_parent, const String &p_filename, BitField<FileListener::FileTypeFlags> p_type);
	void _handle_action(DirWatchInotify *p_watch, const String &p_filename, BitField<FileListener::FileTypeFlags> p_type, uint64_t p_action, const String &p_old_path = String());
	void _process_event(const struct inotify_event *event);
	void _process_buffered_events(const char *p_buffer, ssize_t p_total_bytes);

	virtual bool _run() override;
	virtual void _start_watch_internally() override;
	virtual void _stop_watch_internally() override;

	void _update_watch_paths(const String &p_old_path, const String &p_new_path);

	virtual bool _check_already_watched(const String &p_dir_path, BitField<FileListener::FileTypeFlags> p_type, const String &p_real_path) override;

	virtual void _complete_adding_for(DirWatch *p_watch) override;
	virtual WatchID _create_watch(const String &p_dir_path, bool p_recursive, DirWatch **r_watch) override;
	void _add_recursive_child_watch(const String &p_dir_path, const Ref<FileListener> &p_listener, DirWatchInotify *p_parent);

	void _destroy_watch(DirWatchInotify *p_watch, bool p_skip);
	void _erase(WatchID p_id, bool p_skip, bool p_recursive_skip = true);
	virtual void _remove_watch(WatchID p_id) override;
	void _clear_watches();

protected:
	static Ref<FileWatcher> _create_func();

public:
	static void make_default();

	FileWatcherInotify();
	~FileWatcherInotify() override;
};

#endif // __linux__
