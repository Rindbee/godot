/**************************************************************************/
/*  file_watcher_inotify.cpp                                              */
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

#ifdef __linux__

#include "file_watcher_inotify.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"

#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>

static constexpr size_t INOTIFY_BUFF_SIZE = (sizeof(struct inotify_event) + NAME_MAX + 1) * 1024;

void FileWatcherInotify::_clear_move_events(DirWatchInotify *p_for_watch) {
	if (moves.is_empty()) {
		return;
	}

	if (p_for_watch) {
		print_verbose(vformat("Clear unmatched move from events in the watch %d.", p_for_watch->id));
	} else {
		print_verbose(vformat("Clear unmatched move from events. caller_id: %d, inotify_fd: %d.", Thread::get_caller_id(), inotify_fd));
	}

	// The affected unmatched MOVE FROM events will be handled as DELETE actions.
	List<PendingMoveEvent> to_remove_events;

	const uint64_t now = OS::get_singleton()->get_ticks_msec();

	HashMap<uint32_t, PendingMoveEvent>::Iterator next = moves.begin();
	HashMap<uint32_t, PendingMoveEvent>::Iterator I;
	while (next) {
		I = next;
		++next;
		PendingMoveEvent &move_from = I->value;
		if ((p_for_watch && (move_from.watch == p_for_watch)) || (!p_for_watch && ((now - move_from.timestamp_ms) > MATCH_TIMEOUT_MS))) {
			to_remove_events.push_back(move_from);
			moves.remove(I);
		}
	}

	if (to_remove_events.is_empty()) {
		return;
	}

	for (PendingMoveEvent &E : to_remove_events) {
		_handle_action(E.watch, E.filename, E.type, IN_DELETE);
	}
}

void FileWatcherInotify::_clear_pending_events() {
	if (is_deduplicated()) {
		prev_event.modified = false;
		prev_event.name.clear();
		prev_event.wd = UNAVAILABLE_WATCHER;
	}

	_clear_move_events(nullptr);
}

void FileWatcherInotify::_overflow() {
	for (KeyValue<WatchID, DirWatch *> &KV : watches) {
		KV.value->listener->missing_actions_handle(KV.value->id, KV.value->path);
	}
}

void FileWatcherInotify::_check_new_watch_for(DirWatchInotify *p_parent, const String &p_filename, BitField<FileListener::FileTypeFlags> p_type) {
	if (!p_type.has_flag(FileListener::IS_DIR)) {
		return;
	}

	ERR_FAIL_NULL(p_parent);
	print_verbose(vformat("Check new watch for \"%s\" in  \"%s\"", p_filename, p_parent->path));

	if (!p_parent->recursive) {
		return;
	}

	StringBuilder sb;
	sb += p_parent->path;
	sb += p_filename;
	sb += "/";
	const String &dir_path = sb.as_string();
	if (watch_ids.has(dir_path)) {
		return;
	}

	_add_recursive_child_watch(dir_path, p_parent->listener, p_parent);
}

void FileWatcherInotify::_handle_action(DirWatchInotify *p_watch, const String &p_filename, BitField<FileListener::FileTypeFlags> p_type, uint64_t p_action, const String &p_old_path) {
	ERR_FAIL_NULL(p_watch);

	bool refresh = false;

	if (IN_CREATE & p_action) {
		refresh = p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_ADD);
		_check_new_watch_for(p_watch, p_filename, p_type);
	} else if (IN_DELETE & p_action) {
		// If the file erased is a directory, removes the directory erased.
		if (p_type.has_flag(FileListener::IS_DIR)) {
			StringBuilder sb;
			sb += p_watch->path;
			sb += p_filename;
			sb += "/";
			const String &dir_path = sb.as_string();
			HashMap<String, WatchID>::Iterator I = watch_ids.find(dir_path);
			if (I) {
				_remove_watch(I->value);
			}
		}
		refresh = p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_DELETE);
	} else if ((IN_CLOSE_WRITE | IN_MODIFY) & p_action) {
		refresh = p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_MODIFIED);
	} else if (IN_MOVED_TO & p_action) {
		// If p_old_path doesn't exist means that the file has been moved in from outside the scope.
		if (p_old_path.is_empty()) {
			refresh |= p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_ADD);
			refresh |= p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_MODIFIED);
			_check_new_watch_for(p_watch, p_filename, p_type);
		} else {
			refresh = p_watch->listener->file_action_handle(p_watch->id, p_watch->path, p_filename, p_type, FileListener::ACTION_MOVED, p_old_path);

			if (p_type.has_flag(FileListener::IS_DIR) && p_watch->recursive) {
				// Update the path.
				StringBuilder sb;
				sb += p_watch->path;
				sb += p_filename;
				sb += "/";
				const String &new_dir_path = sb.as_string();
				_update_watch_paths(p_old_path + "/", new_dir_path);
			}
		}
	}

	if (refresh) {
		_scan_dir_contents(p_watch);
	}
}

void FileWatcherInotify::_process_event(const struct inotify_event *event) {
	// Process individual event here.
	DirWatchInotify *watch = nullptr;

	const String &filename = event->len > 0 ? String::utf8(event->name, event->len - 1) : String();
	print_verbose(vformat("Event received: wd: %d, name: \"%s\", cookie: %X, mask: %X", event->wd, filename, event->cookie, event->mask));

	BitField<FileListener::FileTypeFlags> type = 0;
	if (IN_ISDIR & event->mask) {
		type.set_flag(FileListener::IS_DIR);
	}

	HashMap<WatchID, DirWatchInotify *>::Iterator I = all_watches.find(event->wd);

	if (I) {
		watch = I->value;
		if (IN_IGNORED & event->mask) {
			_erase(event->wd, true); // The watch was automatically removed by the kernel.
			return;
		}

		//  Check if it is a symlink pointing to a directory.
		if (!type.has_flag(FileListener::IS_DIR) && ((IN_DELETE | IN_MOVED_FROM) & event->mask)) {
			StringBuilder sb;
			sb += watch->path;
			sb += filename;
			sb += "/";
			const String &dir_path = sb.as_string();
			if (watch_ids.has(dir_path)) {
				type.set_flag(FileListener::IS_DIR | FileListener::IS_LINK);
			}
		}
	} else {
		if (IN_IGNORED & event->mask) {
			return; // The watch has been manually removed.
		}
		ERR_PRINT(vformat("Watch lost unexpectedly: wd: %d, name: \"%s\", cookie: %X, mask: %X", event->wd, filename, event->cookie, event->mask));
		return;
	}

	ERR_FAIL_NULL(watch);

	if (!((IN_DELETE | IN_MOVED_FROM) & event->mask) && !type.has_flag(FileListener::IS_DIR)) {
		//  Check if it is a symlink pointing to a directory.
		const String dir_path = watch->path + filename;
		Ref<DirAccess> d = DirAccess::create_for_path(dir_path);
		if (d.is_valid()) {
			if (d->is_link(dir_path)) {
				type.set_flag(FileListener::IS_LINK);
			}
			if (d->dir_exists(dir_path)) {
				type.set_flag(FileListener::IS_DIR);
			}
		}
	}

	if (watch->listener->file_event_filter(watch->id, watch->path, filename, type)) {
		return; // Custom event filtering logic.
	}

	if (event->mask & IN_MOVED_FROM) {
		HashMap<uint32_t, PendingMoveEvent>::Iterator E = moves.find(event->cookie);
		if (E) {
			PendingMoveEvent &move_from = E->value;
			if (all_watches.has(move_from.wd)) {
				WARN_PRINT(vformat("An event with the same cookie already exists, wd: %d, dir path: \"%s\", file name: \"%s\".", move_from.wd, move_from.watch->path, move_from.filename));
				_handle_action(move_from.watch, move_from.filename, move_from.type, IN_DELETE);
			} else {
				ERR_PRINT(vformat("An event with the same cookie %X for \"%s\" already exists, but the watch (wd %d) was accidentally lost.", event->cookie, move_from.filename, move_from.wd));
			}
		}
		moves[event->cookie] = PendingMoveEvent{ watch->wd, watch, filename, type, OS::get_singleton()->get_ticks_msec() };
		return;
	}

	StringBuilder old_path;
	if (event->mask & IN_MOVED_TO) {
		// Try to find a matching FROM event from moves.
		HashMap<uint32_t, PendingMoveEvent>::Iterator E = moves.find(event->cookie);
		if (E) {
			PendingMoveEvent &move_from = E->value;
			if (all_watches.has(move_from.wd)) {
				old_path += move_from.watch->path;
				old_path += move_from.filename;
				type = move_from.type;
			} else {
				ERR_PRINT(vformat("The watch (wd %d) was accidentally lost, but the MOVE FROM event for \"%s\" with cookie %X was not cleared.", move_from.wd, move_from.filename, event->cookie));
			}
			moves.remove(E);
		}
	}

	_handle_action(watch, filename, type, event->mask, old_path.as_string());
}

void FileWatcherInotify::_process_buffered_events(const char *p_buffer, ssize_t p_total_bytes) {
	ssize_t prev_pos = 0;
	ssize_t current_pos = 0;

	while (current_pos < p_total_bytes) {
		struct inotify_event *event = (struct inotify_event *)&p_buffer[current_pos];

		ERR_BREAK_MSG(event->len > NAME_MAX + 1, vformat("Suspicious filename length: %u, skipping event.", event->len));

		prev_pos = current_pos;
		current_pos += sizeof(struct inotify_event) + event->len;

		ERR_BREAK_MSG(current_pos <= prev_pos, "Arithmetic overflow detected in event stream");
		ERR_BREAK_MSG(current_pos > p_total_bytes, "Buffer overflow in inotify event stream.");

		if (is_deduplicated()) {
			const String name = event->len > 0 ? String::utf8(event->name, event->len - 1) : String();
			bool modified = (IN_MODIFY | IN_CLOSE_WRITE) & event->mask;
			if (prev_event.modified && modified && prev_event.wd == event->wd && prev_event.name == name) {
				print_verbose(vformat("Duplicated modified event skipped: wd: %d, name: \"%s\", cookie: %X, mask: %X", event->wd, name, event->cookie, event->mask));
				continue;
			}
			prev_event.modified = modified;
			prev_event.wd = event->wd;
			prev_event.name = name;
		}

		if (IN_Q_OVERFLOW & event->mask) {
			_overflow();
			continue;
		}

		_process_event(event);
	}
}

bool FileWatcherInotify::_run() {
	if (!running.is_set()) {
		return false;
	}

	_flush_pending_watch_actions();

	struct pollfd pfd;
	pfd.fd = inotify_fd;
	pfd.events = POLLIN;

	int ret = poll(&pfd, 1, QUERY_TIMEOUT_MS);

	if (ret <= 0 || !(pfd.revents & POLLIN)) {
		_clear_pending_events();
		return true;
	}

	while (true) {
		ssize_t len = read(inotify_fd, buffer, INOTIFY_BUFF_SIZE);
		if (len <= 0) {
			const int read_err = errno;
			if (len < 0 && read_err != EAGAIN) {
				ERR_PRINT(vformat("Inotify read failed: %s.", String(strerror(read_err))));
			}
			return true;
		}

		_process_buffered_events(buffer, len);

		if (len < (ssize_t)INOTIFY_BUFF_SIZE) {
			return true;
		}
	}

	return true;
}

void FileWatcherInotify::_start_watch_internally() {
	buffer = (char *)memalloc(INOTIFY_BUFF_SIZE);
	memset(buffer, 0, INOTIFY_BUFF_SIZE);
}

void FileWatcherInotify::_stop_watch_internally() {
	memfree(buffer);
}

void FileWatcherInotify::_update_watch_paths(const String &p_old_path, const String &p_new_path) {
	for (const KeyValue<WatchID, DirWatchInotify *> &KV : all_watches) {
		if (!KV.value->path.begins_with(p_old_path)) {
			continue;
		}

		const String &new_subdir_path = p_new_path.path_join(KV.value->path.substr(p_old_path.length()));
		print_verbose(vformat("Update the watch path from \"%s\" to \"%s\".", KV.value->path, new_subdir_path));
		watch_ids.erase(KV.value->path);
		KV.value->path = new_subdir_path;
		watch_ids[KV.value->path] = KV.key;
	}
}

bool FileWatcherInotify::_check_already_watched(const String &p_dir_path, BitField<FileListener::FileTypeFlags> p_type, const String &p_real_path) {
	for (const KeyValue<WatchID, DirWatchInotify *> &KV : all_watches) {
		if (KV.value->real_path != p_real_path) {
			continue;
		}

		if (KV.value->id == KV.key) {
			print_verbose(KV.value->type.has_flag(FileListener::IS_LINK)
							? vformat("Directory \"%s\" is already being manually watched via link \"%s\"; the auto attempt to add watch via \"%s\" failed.", p_real_path, KV.value->path, p_dir_path)
							: vformat("Directory \"%s\" is already being manually watched; the auto attempt to add watch via \"%s\" failed.", p_real_path, p_dir_path));
			return true;
		}

		if (!KV.value->type.has_flag(FileListener::IS_LINK) || p_type.has_flag(FileListener::IS_LINK)) {
			print_verbose(KV.value->type.has_flag(FileListener::IS_LINK)
							? vformat("Directory \"%s\" is already being auto watched via link \"%s\"; the auto attempt to add watch via link \"%s\" failed.", p_real_path, KV.value->path, p_dir_path)
							: vformat("Directory \"%s\" is already being auto watched; the auto attempt to add watch via link \"%s\" failed.", p_real_path, p_dir_path));
			return true;
		}

		print_verbose(vformat("Directory \"%s\" is already being auto watched via link \"%s\"; the real path is prioritized for adding the watch over the symlink as it also falls within the scope.", p_real_path, KV.value->path));

		// If both are within the watch scope, the real path is prioritized over the symlink.
		_update_watch_paths(KV.value->path, p_dir_path);
		return true;
	}

	return false;
}

void FileWatcherInotify::_scan_dir_contents(DirWatchInotify *p_watch) {
	Ref<DirAccess> da = DirAccess::open(p_watch->path);
	ERR_FAIL_COND(da.is_null());

	da->list_dir_begin();
	while (ready.is_set()) {
		String f = da->get_next();
		if (f.is_empty()) {
			break;
		}

		if (f == "." || f == "..") {
			continue;
		}

		BitField<FileListener::FileTypeFlags> sub_type = 0;
		if (da->current_is_dir()) {
			sub_type.set_flag(FileListener::IS_DIR);
		}
		if (da->is_link(f)) {
			sub_type.set_flag(FileListener::IS_LINK);
		}

		if (p_watch->listener->file_event_filter(p_watch->id, p_watch->path, f, sub_type)) {
			continue; // Custom filtering rules.
		} else {
			p_watch->listener->file_action_handle(p_watch->id, p_watch->path, f, sub_type, FileListener::ACTION_ADD);
		}

		if (p_watch->recursive) {
			if (!sub_type.has_flag(FileListener::IS_DIR) || !da->is_readable(f)) {
				continue;
			}

			if (!is_following_symlinks() && sub_type.has_flag(FileListener::IS_LINK)) {
				continue;
			}

			_add_recursive_child_watch(p_watch->path + f + "/", p_watch->listener, p_watch);
		}
	}
	da->list_dir_end();
}

void FileWatcherInotify::_complete_adding_for(DirWatch *p_watch) {
	DirWatchInotify *watch = static_cast<DirWatchInotify *>(p_watch);
	ERR_FAIL_NULL(watch);

	// Handle watch ID reuse (dirs moving between watching directories).
	if (all_watches.has(watch->wd)) {
		_erase(watch->wd, true, watch->recursive);
	}

	all_watches[watch->wd] = watch;
	watch_ids[watch->path] = watch->wd;
	if (!watch->parent) {
		watches[watch->id] = watch;
	}

	_scan_dir_contents(watch);
}

WatchID FileWatcherInotify::_create_watch(const String &p_dir_path, bool p_recursive, DirWatch **r_watch) {
	if (!ready.is_set()) {
		return UNAVAILABLE_WATCHER;
	}

	uint32_t mask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;

	int wd = inotify_add_watch(inotify_fd, p_dir_path.utf8().get_data(), mask);

	if (wd < 0) {
		const int watch_err = errno; // Cache this value to prevent accidental modification.
		switch (watch_err) {
			case ENOENT:
				ERR_FAIL_V_MSG(DIR_NOT_FOUND, vformat("Directory \"%s\" does not exist.", p_dir_path));
			case EACCES:
				ERR_FAIL_V_MSG(DIR_NOT_READABLE, vformat("Directory \"%s\" is not readable.", p_dir_path));
			case ENOSPC:
				ERR_FAIL_V_MSG(WATCH_FAILED, vformat("Inotify watch limit reached while adding watch for \"%s\"", p_dir_path));
			case ENOMEM:
				ERR_FAIL_V_MSG(WATCH_FAILED, vformat("Insufficient memory to add watch for \"%s\"", p_dir_path));
			default:
				ERR_FAIL_V_MSG(WATCH_FAILED, vformat("Failed to add inotify watch for \"%s\": %s", p_dir_path, String(strerror(watch_err))));
		}
	}

	DirWatchInotify *watch = memnew(DirWatchInotify);
	watch->id = wd;
	watch->path = p_dir_path;
	watch->recursive = p_recursive;
	watch->wd = wd;

	if (r_watch) {
		*r_watch = watch;
	}

	return wd;
}

void FileWatcherInotify::_add_recursive_child_watch(const String &p_dir_path, const Ref<FileListener> &p_listener, DirWatchInotify *p_parent) {
	if (!ready.is_set()) {
		return;
	}
	ERR_FAIL_NULL(p_parent);

	BitField<FileListener::FileTypeFlags> type = 0;
	String real_dir_path;

	WatcherError err = _check_path_watchable(p_dir_path, type, real_dir_path, all_watches[p_parent->id]->path);
	if (err != NO_WATCH_ERROR) {
		return;
	}

	DirWatch *base_watch = nullptr;
	WatchID wd = _create_watch(p_dir_path, true, &base_watch);
	if (wd < 0) {
		return;
	}

	DirWatchInotify *watch = static_cast<DirWatchInotify *>(base_watch);
	ERR_FAIL_NULL(watch);

	watch->id = p_parent->id;
	watch->type = type;
	watch->real_path = real_dir_path;
	watch->listener = p_listener;
	watch->parent = p_parent;
	p_parent->children.push_back(watch);

	print_verbose(watch->type.has_flag(FileListener::IS_LINK)
					? vformat("Add watch for \"%s\" (real: \"%s\") in scope \"%s\" (id: %d) with wd: %d.", watch->path, watch->real_path, all_watches[p_parent->id]->path, watch->id, watch->wd)
					: vformat("Add watch for \"%s\" in scope \"%s\" (id: %d) with wd: %d.", watch->path, all_watches[p_parent->id]->path, watch->id, watch->wd));

	_complete_adding_for(watch);
}

void FileWatcherInotify::_destroy_watch(DirWatchInotify *p_watch, bool p_skip) {
	print_verbose(vformat("Destroy watch for \"%s\" with id: %d, wd: %d.", p_watch->path, p_watch->id, p_watch->wd));

	const WatchID wd = p_watch->wd;
	if (p_watch->parent == nullptr) {
		watches.erase(wd);
	}

	watch_ids.erase(p_watch->path);
	all_watches.erase(wd);

	memdelete(p_watch);

	if (p_skip) {
		return;
	}

	int err = inotify_rm_watch(inotify_fd, wd);
	const int watch_err = errno;
	ERR_FAIL_COND_MSG(err < 0 && watch_err != EINVAL, vformat("Error removing inotify watch %d: %s", wd, String(strerror(watch_err))));
}

void FileWatcherInotify::_erase(WatchID p_id, bool p_skip, bool p_recursive_skip) {
	if (!ready.is_set()) {
		return;
	}

	HashMap<WatchID, DirWatchInotify *>::Iterator I = all_watches.find(p_id);
	ERR_FAIL_NULL_MSG(I, vformat("The watch to be removed by %d does not exist.", p_id));
	DirWatchInotify *watch = I->value;
	ERR_FAIL_NULL_MSG(watch, vformat("The watch to be removed by %d is invalid.", p_id));

	print_verbose(vformat("Remove watch for \"%s\" with id: %d, wd: %d.", watch->path, watch->id, watch->wd));

	if (watch->parent) {
		watch->parent->children.erase(watch);
	}

	if (!watch->recursive) {
		_clear_move_events(watch);
		_destroy_watch(watch, p_skip);
		return;
	}

	List<DirWatchInotify *> to_removes;
	to_removes.push_back(watch);

	List<DirWatchInotify *>::Element *E = to_removes.back();
	while (E) {
		DirWatchInotify *dir = E->get();
		if (dir->recursive) {
			for (DirWatchInotify *sub : dir->children) {
				to_removes.push_front(sub);
			}
		}
		E = E->prev();
	}

	for (DirWatchInotify *F : to_removes) {
		_clear_move_events(F);
		_destroy_watch(F, p_skip && (F->wd == p_id || p_recursive_skip));
	}
}

void FileWatcherInotify::_remove_watch(WatchID p_id) {
	_erase(p_id, false);
}

void FileWatcherInotify::_clear_watches() {
	MutexLock lock(watch_actions_mutex);

	for (const KeyValue<WatchID, DirWatchInotify *> &KV : all_watches) {
		memdelete(KV.value);
	}
	all_watches.clear();
	watch_ids.clear();
	watches.clear();

	// Clean up watches that haven't been added yet.
	for (WatchAction &E : pending_watch_actions) {
#ifdef THREADS_ENABLED
		if (E.done) {
			E.done->post();
		}
#endif // THREADS_ENABLED
		if (E.op == WatchAction::ADD) {
			memdelete(E.watch);
		}
	}
	pending_watch_actions.clear();
}

Ref<FileWatcher> FileWatcherInotify::_create_func() {
	return memnew(FileWatcherInotify);
}

void FileWatcherInotify::make_default() {
	_create = _create_func;

	supported_fs = {
		"EXTFS",
		"XFS",
		"BTRFS",
		"F2FS",
		"BCACHEFS",
		"ZFS",
		"REISERFS",
		"NILFS",
		"FAT32",
		"EXFAT",
		"NTFS",
		// "SMB",
		// "CIFS",
		// "NFS",
	};
}

FileWatcherInotify::FileWatcherInotify() {
	inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	ERR_FAIL_COND(inotify_fd < 0);
	ready.set();
}

FileWatcherInotify::~FileWatcherInotify() {
	if (inotify_fd < 0) {
		return;
	}

	stop_watch();

	ready.clear();

	_clear_watches();

	close(inotify_fd);
	inotify_fd = -1;
}

#endif // __linux__
