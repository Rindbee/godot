/**************************************************************************/
/*  file_watcher.h                                                        */
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

#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

#ifdef THREADS_ENABLED
#include "core/os/semaphore.h"
#endif // THREADS_ENABLED

typedef int WatchID;

class FileListener : public RefCounted {
	GDCLASS(FileListener, RefCounted);

public:
	enum FileAction {
		ACTION_ADD = 1, // A file or directory is created or moved in from outside the scope.
		ACTION_DELETE = 2, // A file or directory is deleted or moved out of scope.
		ACTION_MODIFIED = 3, // A file or directory is modified.
		ACTION_MOVED = 4, // A file or directory is moved.
	};

	enum FileTypeFlags {
		IS_DIR = 1, // Whether it is a directory or points to a valid directory.
		IS_LINK = 2,
	};

	typedef bool (*FileFilterCallback)(WatchID p_id, const String &p_dir_path, const String &p_filename, BitField<FileTypeFlags> p_type);

private:
	Callable event_filter;
	FileFilterCallback event_filter_callback = nullptr;

	Callable file_action_handler;
	Callable missing_actions_handler;

protected:
	static void _bind_methods();

public:
	void set_event_filter(const Callable &p_filter);
	void set_event_filter_callback(FileFilterCallback p_filter);
	bool file_event_filter(WatchID p_id, const String &p_dir_path, const String &p_filename, BitField<FileTypeFlags> p_type);

	void set_file_action_handler(const Callable &p_handler);
	void file_action_handle(WatchID p_id, const String &p_dir_path, const String &p_filename, BitField<FileTypeFlags> p_type, FileAction p_action, const String &p_old_path = String());

	void set_missing_file_actions_handler(const Callable &p_handler);
	void missing_actions_handle(WatchID p_id, const String &p_path);
};

class FileWatcher : public RefCounted {
	GDCLASS(FileWatcher, RefCounted);

public:
	enum WatcherError {
		NO_WATCH_ERROR = 0,
		UNAVAILABLE_WATCHER = -1, // The FileWatcher is not yet available.
		INVALID_LISTENER = -2, // The file listener being used is invalid.
		DIR_NOT_FOUND = -3, // The directory to be watched does not exist or is inaccessible.
		DIR_NOT_READABLE = -4, // The directory to be watched lacks read permissions.
		DIR_ALREADY_WATCHED = -5, // The directory to be watched has already been manually added for watching.
		FS_UNSUPPORTED = -6, // The directory to be watched is in a unsupported file system.
		WATCH_FAILED = -7, // File system watcher failed to watch for changes.
		FOLLOW_LINK_NOT_ALLOWED = -8, // Symlink following is disabled.
		OUT_OF_SCOPE = -9, // The symlink points outside the allowed watch scope.
	};

protected:
	SafeFlag ready;

	struct DirWatch {
		WatchID id = UNAVAILABLE_WATCHER;
		String path;
		BitField<FileListener::FileTypeFlags> type = 0;
		String real_path;
		Ref<FileListener> listener;
		bool recursive = false;

		virtual ~DirWatch() = default;
	};

	HashMap<String, WatchID> watch_ids;
	HashMap<WatchID, DirWatch *> watches;

	struct WatchAction {
		enum WatchOperations {
			ADD,
			REMOVE,
			FLUSH,
		};
		WatchOperations op = ADD;
		WatchID id = UNAVAILABLE_WATCHER;
		DirWatch *watch = nullptr;
#ifdef THREADS_ENABLED
		Semaphore *done = nullptr;
#endif // THREADS_ENABLED
	};

	Mutex watch_actions_mutex;
	List<WatchAction> pending_watch_actions;

#ifdef THREADS_ENABLED
#ifdef TOOLS_ENABLED
	static const int QUERY_TIMEOUT_MS = 100;
#else
	static const int QUERY_TIMEOUT_MS = 500;
#endif // TOOLS_ENABLED
#else
	static const int QUERY_TIMEOUT_MS = 0;
#endif // THREADS_ENABLED

	SafeFlag running;

	static Ref<FileWatcher> (*_create)();
	static HashSet<String> supported_fs;

private:
	bool follow_symlinks = false;
	bool allow_out_of_scope_links = false;
	bool skip_fs_type_check = false;
	bool deduplicated = true;

#ifdef THREADS_ENABLED
	Thread thread;
	static void _thread_func(void *_userdata);
#endif // THREADS_ENABLED

	void _flush();

	bool _check_fs_unsupported(const String &p_dir_path);

protected:
	static void _bind_methods();

	void _flush_pending_watch_actions();
	virtual bool _run() = 0;
	virtual void _start_watch_internally() {}
	virtual void _stop_watch_internally() {}

	virtual bool _check_already_watched(const String &p_dir_path, BitField<FileListener::FileTypeFlags> p_type, const String &p_real_path);
	WatcherError _check_path_watchable(const String &p_path, BitField<FileListener::FileTypeFlags> &r_type, String &r_real_path, const String &p_scope = String());

	virtual void _complete_adding_for(DirWatch *p_watch);
	virtual WatchID _create_watch(const String &p_dir_path, bool p_recursive, DirWatch **r_watch) = 0;
	virtual void _remove_watch(WatchID p_id) = 0;

public:
	WatchID add_watch(const String &p_dir_path, const Ref<FileListener> &p_listener, bool p_recursive = true);
	TypedArray<String> get_watch_directories() const;
	void remove_watch_by_id(WatchID p_id);
	void remove_watch_by_path(const String &p_dir_path);

	void watch();
	void stop_watch();
	bool is_watching() const;

	void poll_events();

	void set_follow_symlinks(bool p_follow) { follow_symlinks = p_follow; }
	bool is_following_symlinks() const { return follow_symlinks; }

	void set_allow_out_of_scope_links(bool p_allow) { allow_out_of_scope_links = p_allow; }
	bool is_allowing_out_of_scope_links() const { return allow_out_of_scope_links; }

	void set_skip_fs_type_check(bool p_skip) { skip_fs_type_check = p_skip; }
	bool is_skipping_fs_type_check() const { return skip_fs_type_check; }

	void set_deduplicated(bool p_deduplicated) { deduplicated = p_deduplicated; }
	bool is_deduplicated() const { return deduplicated; }

	static Ref<FileWatcher> create();

	FileWatcher() {}
	virtual ~FileWatcher() {}
};

VARIANT_ENUM_CAST(FileListener::FileAction);
VARIANT_ENUM_CAST(FileListener::FileTypeFlags);
VARIANT_ENUM_CAST(FileWatcher::WatcherError);
