/**************************************************************************/
/*  file_watcher.cpp                                                      */
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

#include "file_watcher.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/object/class_db.h"

void FileListener::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_filter", "filter"), &FileListener::set_event_filter);
	ClassDB::bind_method(D_METHOD("set_file_action_handler", "handler"), &FileListener::set_file_action_handler);
	ClassDB::bind_method(D_METHOD("set_missing_file_actions_handler", "handler"), &FileListener::set_missing_file_actions_handler);

	BIND_ENUM_CONSTANT(ACTION_ADD);
	BIND_ENUM_CONSTANT(ACTION_DELETE);
	BIND_ENUM_CONSTANT(ACTION_MODIFIED);
	BIND_ENUM_CONSTANT(ACTION_MOVED);

	BIND_ENUM_CONSTANT(IS_DIR);
	BIND_ENUM_CONSTANT(IS_LINK);
}

void FileListener::set_event_filter(const Callable &p_filter) {
	event_filter = p_filter;
}

void FileListener::set_event_filter_callback(FileFilterCallback p_filter) {
	event_filter_callback = p_filter;
}

bool FileListener::file_event_filter(WatchID p_id, const String &p_dir_path, const String &p_filename, BitField<FileTypeFlags> p_type) {
	if (event_filter_callback) {
		return event_filter_callback(p_id, p_dir_path, p_filename, p_type);
	}

	if (event_filter.is_valid()) {
		return event_filter.call(p_id, p_dir_path, p_filename, p_type);
	}

	return false;
}

void FileListener::set_file_action_handler(const Callable &p_handler) {
	file_action_handler = p_handler;
}

bool FileListener::file_action_handle(WatchID p_id, const String &p_dir_path, const String &p_filename, BitField<FileTypeFlags> p_type, FileAction p_action, const String &p_old_path) {
	if (file_action_handler.is_valid()) {
		return file_action_handler.call(p_id, p_dir_path, p_filename, p_type, p_action, p_old_path);
	}

	return false;
}

void FileListener::set_missing_file_actions_handler(const Callable &p_handler) {
	missing_actions_handler = p_handler;
}

void FileListener::missing_actions_handle(WatchID p_id, const String &p_path) {
	if (missing_actions_handler.is_valid()) {
		missing_actions_handler.call(p_id, p_path);
	}
}

Ref<FileWatcher> (*FileWatcher::_create)() = nullptr;
HashSet<String> FileWatcher::supported_fs;

Ref<FileWatcher> FileWatcher::create() {
	if (_create) {
		return _create();
	}

	ERR_PRINT("Unable to create file watcher, platform not supported.");
	return nullptr;
}

void FileWatcher::_bind_methods() {
	ClassDB::bind_static_method("FileWatcher", D_METHOD("create"), &FileWatcher::create);

	ClassDB::bind_method(D_METHOD("add_watch", "directory", "listener", "recursive"), &FileWatcher::add_watch, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_watch_directories"), &FileWatcher::get_watch_directories);
	ClassDB::bind_method(D_METHOD("remove_watch_by_id", "watch_id"), &FileWatcher::remove_watch_by_id);
	ClassDB::bind_method(D_METHOD("remove_watch_by_path", "directory"), &FileWatcher::remove_watch_by_path);

	ClassDB::bind_method(D_METHOD("watch"), &FileWatcher::watch);
	ClassDB::bind_method(D_METHOD("stop_watch"), &FileWatcher::stop_watch);
	ClassDB::bind_method(D_METHOD("poll_events"), &FileWatcher::poll_events);

	ClassDB::bind_method(D_METHOD("set_allow_out_of_scope_links", "enable"), &FileWatcher::set_allow_out_of_scope_links);
	ClassDB::bind_method(D_METHOD("is_allowing_out_of_scope_links"), &FileWatcher::is_allowing_out_of_scope_links);
	ClassDB::bind_method(D_METHOD("set_deduplicated", "enable"), &FileWatcher::set_deduplicated);
	ClassDB::bind_method(D_METHOD("is_deduplicated"), &FileWatcher::is_deduplicated);
	ClassDB::bind_method(D_METHOD("set_follow_symlinks", "enable"), &FileWatcher::set_follow_symlinks);
	ClassDB::bind_method(D_METHOD("is_following_symlinks"), &FileWatcher::is_following_symlinks);
	ClassDB::bind_method(D_METHOD("set_skip_fs_type_check", "enable"), &FileWatcher::set_skip_fs_type_check);
	ClassDB::bind_method(D_METHOD("is_skipping_fs_type_check"), &FileWatcher::is_skipping_fs_type_check);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_out_of_scope_links"), "set_allow_out_of_scope_links", "is_allowing_out_of_scope_links");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "deduplicated"), "set_deduplicated", "is_deduplicated");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_symlinks"), "set_follow_symlinks", "is_following_symlinks");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "skip_fs_type_check"), "set_skip_fs_type_check", "is_skipping_fs_type_check");

	ADD_PROPERTY_DEFAULT("allow_out_of_scope_links", false);
	ADD_PROPERTY_DEFAULT("deduplicated", true);
	ADD_PROPERTY_DEFAULT("follow_symlinks", false);
	ADD_PROPERTY_DEFAULT("skip_fs_type_check", false);

	BIND_ENUM_CONSTANT(NO_WATCH_ERROR);
	BIND_ENUM_CONSTANT(UNAVAILABLE_WATCHER);
	BIND_ENUM_CONSTANT(INVALID_LISTENER);
	BIND_ENUM_CONSTANT(DIR_NOT_FOUND);
	BIND_ENUM_CONSTANT(DIR_NOT_READABLE);
	BIND_ENUM_CONSTANT(DIR_ALREADY_WATCHED);
	BIND_ENUM_CONSTANT(FS_UNSUPPORTED);
	BIND_ENUM_CONSTANT(WATCH_FAILED);
	BIND_ENUM_CONSTANT(FOLLOW_LINK_NOT_ALLOWED);
	BIND_ENUM_CONSTANT(OUT_OF_SCOPE);
}

void FileWatcher::_complete_adding_for(DirWatch *p_watch) {
	watch_ids[p_watch->path] = p_watch->id;
	watches[p_watch->id] = p_watch;
}

void FileWatcher::_flush_pending_watch_actions() {
	MutexLock lock(watch_actions_mutex);
	if (pending_watch_actions.is_empty()) {
		return;
	}

	for (WatchAction &E : pending_watch_actions) {
#ifdef THREADS_ENABLED
		if (E.done) {
			E.done->post();
		}
#endif // THREADS_ENABLED
		if (E.op == WatchAction::ADD) {
			_complete_adding_for(E.watch);
		} else if (E.op == WatchAction::REMOVE) {
			_remove_watch(E.id);
		}
	}
	pending_watch_actions.clear();
}

void FileWatcher::_flush() {
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");
#ifdef THREADS_ENABLED
	if (!is_watching()) {
		_flush_pending_watch_actions();
		return;
	}

	Semaphore flushed;
	{
		MutexLock lock(watch_actions_mutex);
		if (!running.is_set()) {
			return;
		}
		pending_watch_actions.push_back({ WatchAction::FLUSH, 0, nullptr, &flushed });
	}
	flushed.wait();
#else
	_flush_pending_watch_actions();
#endif // THREADS_ENABLED
}

#ifdef THREADS_ENABLED
void FileWatcher::_thread_func(void *_userdata) {
	FileWatcher *watcher = (FileWatcher *)_userdata;
	while (watcher->_run()) {
	}
}
#endif // THREADS_ENABLED

bool FileWatcher::_check_fs_unsupported(const String &p_dir_path) {
	Ref<DirAccess> da = DirAccess::open(p_dir_path);
	ERR_FAIL_COND_V(da.is_null(), true);

	const String &fs_type = da->get_filesystem_type();
	const bool is_supported = supported_fs.has(fs_type);

	print_verbose(vformat("The file system type \"%s\" for directory \"%s\" is %s.", fs_type, p_dir_path, is_supported ? "supported" : "unsupported"));

	return !is_supported;
}

bool FileWatcher::_check_already_watched(const String &p_dir_path, BitField<FileListener::FileTypeFlags> p_type, const String &p_real_path) {
	{
		MutexLock lock(watch_actions_mutex);
		for (const KeyValue<WatchID, DirWatch *> &KV : watches) {
			ERR_FAIL_COND_V_MSG(KV.value->real_path == p_real_path, true,
					KV.value->type.has_flag(FileListener::IS_LINK)
							? vformat("Directory \"%s\" is already being watched via link \"%s\"; the attempt to add watch via \"%s\" failed.", p_real_path, KV.value->path, p_dir_path)
							: vformat("Directory \"%s\" is already being watched; the attempt to add watch via \"%s\" failed.", p_real_path, p_dir_path));
		}
	}
	return false;
}

FileWatcher::WatcherError FileWatcher::_check_path_watchable(const String &p_path, BitField<FileListener::FileTypeFlags> &r_type, String &r_real_path, const String &p_scope) {
	r_type = 0;
	r_real_path = p_path;

	Ref<DirAccess> d = DirAccess::create_for_path(p_path);
	ERR_FAIL_COND_V_MSG(d.is_null(), FS_UNSUPPORTED, vformat("Cannot create DirAccess for path \"%s\".", p_path));

	// For symbolic links, resolve the actual path to watch.
	if (d->is_link(p_path)) {
		r_type.set_flag(FileListener::IS_LINK);

		r_real_path = d->read_link(p_path);
		if (!r_real_path.is_absolute_path()) {
			const String base_path = p_path.left(-1).get_base_dir();
			r_real_path = base_path.path_join(r_real_path).simplify_path();
		}
		r_real_path = r_real_path.path_join(""); // Ensure directory paths consistently end with '/'.
	}

	ERR_FAIL_COND_V_MSG(!d->dir_exists(p_path), DIR_NOT_FOUND, vformat("Directory \"%s\" does not exist.", p_path));
	r_type.set_flag(FileListener::IS_DIR);

	ERR_FAIL_COND_V_MSG(!d->is_readable(p_path), DIR_NOT_READABLE, vformat("Directory \"%s\" is not readable.", p_path));

	if (p_scope.is_empty()) {
		if (FileWatcher::_check_already_watched(p_path, r_type, r_real_path)) {
			return DIR_ALREADY_WATCHED;
		}
	} else { // Recursive child watches also need to be managed.
		if (r_type.has_flag(FileListener::IS_LINK)) {
			// Additional checks for symlink behavior based on configuration.
			if (!is_following_symlinks()) {
				print_verbose(vformat("Cannot add watch for \"%s\" (real: \"%s\") as follow symlinks is disabled.", p_path, r_real_path));
				return FOLLOW_LINK_NOT_ALLOWED;
			}

			if (!is_allowing_out_of_scope_links() && (!r_real_path.begins_with(p_scope) || r_real_path.length() == p_scope.length())) {
				print_verbose(vformat("Cannot add watch for \"%s\" (real: \"%s\") as it's out of scope \"%s\".", p_path, r_real_path, p_scope));
				return OUT_OF_SCOPE;
			}
		}

		// Check in all watches.
		if (_check_already_watched(p_path, r_type, r_real_path)) {
			return DIR_ALREADY_WATCHED;
		}
	}

	if (!skip_fs_type_check && _check_fs_unsupported(p_path)) {
		return FS_UNSUPPORTED;
	}

	return NO_WATCH_ERROR;
}

WatchID FileWatcher::add_watch(const String &p_dir_path, const Ref<FileListener> &p_listener, bool p_recursive) {
	ERR_FAIL_COND_V_MSG(!ready.is_set(), UNAVAILABLE_WATCHER, "The file watcher is not yet available.");
	ERR_FAIL_COND_V_MSG(p_listener.is_null(), INVALID_LISTENER, "The file listener being used is invalid.");

	const String &dir_path = ProjectSettings::get_singleton()->globalize_path(p_dir_path).simplify_path().path_join("");
	print_verbose(vformat("Try adding a %s watch to the path \"%s\" (simplified from \"%s\").", p_recursive ? "recursive" : "non-recursive", dir_path, p_dir_path));

	BitField<FileListener::FileTypeFlags> type = 0;
	String real_path;

	WatcherError err = _check_path_watchable(dir_path, type, real_path);
	if (err != NO_WATCH_ERROR) {
		return err;
	}

	DirWatch *watch = nullptr;
	WatchID id = _create_watch(dir_path, p_recursive, &watch);
	if (id < 0) {
		return id;
	}

	watch->type = type;
	watch->real_path = real_path;
	watch->listener = p_listener;

	print_verbose(type.has_flag(FileListener::IS_LINK)
					? vformat("Add a %s watch for \"%s\" (real: \"%s\") with id: %d.", p_recursive ? "recursive" : "non-recursive", dir_path, real_path, id)
					: vformat("Add a %s watch for \"%s\" with id: %d.", p_recursive ? "recursive" : "non-recursive", dir_path, id));

	{
		MutexLock lock(watch_actions_mutex);
		pending_watch_actions.push_back({ WatchAction::ADD, id, watch });
	}
	_flush();

	return id;
}

TypedArray<String> FileWatcher::get_watch_directories() const {
	ERR_FAIL_COND_V_MSG(!ready.is_set(), {}, "The file watcher is not yet available.");
	TypedArray<String> ret;
	{
		MutexLock lock(watch_actions_mutex);
		ret.resize(watches.size());
		int idx = 0;
		for (const KeyValue<WatchID, DirWatch *> &KV : watches) {
			ret[idx++] = KV.value->path;
		}
	}
	return ret;
}

void FileWatcher::remove_watch_by_id(WatchID p_id) {
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");

	print_verbose(vformat("Try removing a watch by id: %d.", p_id));

	{
		MutexLock lock(watch_actions_mutex);
		pending_watch_actions.push_back({ WatchAction::REMOVE, p_id });
	}
	_flush();
}

void FileWatcher::remove_watch_by_path(const String &p_dir_path) {
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");

	const String &dir_path = ProjectSettings::get_singleton()->globalize_path(p_dir_path).simplify_path().path_join("");
	print_verbose(vformat("Try removing a watch by the path \"%s\" (simplified from \"%s\").", dir_path, p_dir_path));

	{
		MutexLock lock(watch_actions_mutex);
		HashMap<String, WatchID>::Iterator I = watch_ids.find(dir_path);
		ERR_FAIL_NULL_MSG(I, vformat("Attempt to remove the watch failed. The path \"%s\" (simplified from \"%s\") has not yet been watched.", dir_path, p_dir_path));

		pending_watch_actions.push_back({ WatchAction::REMOVE, I->value });
	}
	_flush();
}

void FileWatcher::watch() {
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");
	if (running.is_set()) {
		return;
	}

#ifdef THREADS_ENABLED
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
#endif // THREADS_ENABLED

	running.set();

	_start_watch_internally();

#ifdef THREADS_ENABLED
	Thread::Settings s;
#ifdef TOOLS_ENABLED
	s.priority = Thread::PRIORITY_NORMAL;
#else
	s.priority = Thread::PRIORITY_LOW;
#endif // TOOLS_ENABLED
	thread.start(_thread_func, this, s);
#endif // THREADS_ENABLED
}

void FileWatcher::stop_watch() {
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");
	if (!running.is_set()) {
		return;
	}
	running.clear();

	_stop_watch_internally();

#ifdef THREADS_ENABLED
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
#endif // THREADS_ENABLED

	_flush_pending_watch_actions();
}

bool FileWatcher::is_watching() const {
#ifdef THREADS_ENABLED
	return thread.is_started() && running.is_set();
#else
	return running.is_set();
#endif // THREADS_ENABLED
}

void FileWatcher::poll_events() {
#ifdef THREADS_ENABLED
	ERR_PRINT("This method should only be used in builds that do not have threading support enabled.");
#else
	ERR_FAIL_COND_MSG(!ready.is_set(), "The file watcher is not yet available.");
	if (!running.is_set()) {
		return;
	}
	_run();
#endif // THREADS_ENABLED
}
