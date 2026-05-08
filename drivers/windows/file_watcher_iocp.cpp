/**************************************************************************/
/*  file_watcher_iocp.cpp                                                 */
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

#ifdef WINDOWS_ENABLED

#include "file_watcher_iocp.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"

void FileWatcherIOCP::_clear_pending_events(DirWatchIOCP *p_for_watch, bool p_is_deletion) {
	HashMap<LONGLONG, PendingEvent> &events = p_is_deletion ? deletions : moves;
	if (events.is_empty()) {
		return;
	}

	if (p_for_watch) {
		print_verbose(vformat("Clear unmatched pending events in the directory \"%s\", id: %d.", p_for_watch->path, p_for_watch->id));
	} else {
		print_verbose(vformat("Clear unmatched pending events. caller_id: %d, iocp_handle: %X.", Thread::get_caller_id(), (LONG_PTR)iocp_handle));
	}

	const uint64_t now = OS::get_singleton()->get_ticks_msec();

	HashMap<LONGLONG, PendingEvent>::Iterator next = events.begin();
	HashMap<LONGLONG, PendingEvent>::Iterator E;
	while (next) {
		E = next;
		++next;
		const PendingEvent &from = E->value;
		if ((p_for_watch && from.watch == p_for_watch) || (!p_for_watch && ((now - from.timestamp_ms) > MATCH_TIMEOUT_MS))) {
			from.watch->listener->file_action_handle(from.id, from.dir_path, from.filename, from.type, FileListener::ACTION_DELETE);
			events.remove(E);
		}
	}
}

void FileWatcherIOCP::_cancel_pending_deletion(const String &p_dir_path, const String &p_filename, LONGLONG p_file_id) {
	HashMap<LONGLONG, PendingEvent>::Iterator next = deletions.begin();
	HashMap<LONGLONG, PendingEvent>::Iterator E;
	while (next) {
		E = next;
		++next;
		const PendingEvent &from = E->value;
		if (from.dir_path == p_dir_path && from.filename == p_filename) {
			deletions.remove(E);
			ERR_CONTINUE_MSG(from.file_id == p_file_id, vformat("Same file id: %X.", p_file_id));
		}
	}
}

void FileWatcherIOCP::_update_info(EventInfo &event, DWORD p_offset, const String &p_scope, const String &p_file_path, DWORD p_action, DWORD p_file_attr, DWORD p_reparse_point_tag, LONGLONG p_file_id) {
	event.offset += p_offset;
	event.has_next = p_offset > 0;
	event.action = p_action;
	event.file_id = p_file_id;

	String base_dir = p_file_path.get_base_dir();
	StringBuilder sb;
	sb += p_scope;
	if (!base_dir.is_empty()) {
		sb += base_dir;
		sb += "/";
	}
	event.dir_path = sb.as_string();
	event.filename = p_file_path.get_file();

	event.type = 0;
	if (p_file_attr & FILE_ATTRIBUTE_DIRECTORY) {
		event.type.set_flag(FileListener::IS_DIR);
	}
	if ((p_reparse_point_tag & IO_REPARSE_TAG_SYMLINK) || (p_file_attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
		event.type.set_flag(FileListener::IS_LINK);
	}
}

void FileWatcherIOCP::_update_info_from_buffer(const DirWatchIOCP *p_watch, EventInfo &event) {
	if (p_watch->extended) {
		PFILE_NOTIFY_EXTENDED_INFORMATION info = (PFILE_NOTIFY_EXTENDED_INFORMATION)&p_watch->buffer[event.offset];
		const int char_count = info->FileNameLength / sizeof(WCHAR);
		event.skip_current = char_count == 0;
		if (event.skip_current) {
			return;
		}

		const String &file_path = String::utf16((const char16_t *)info->FileName, char_count).replace_char('\\', '/');
		_update_info(event, info->NextEntryOffset, p_watch->path, file_path, info->Action, info->FileAttributes, info->ReparsePointTag, info->FileId.QuadPart);

		print_verbose(vformat("Event received for watch %d (\"%s\"): dir: \"%s\", name: \"%s\", attributes: %x, file id: %X, action: %X", p_watch->id, p_watch->path, event.dir_path, event.filename, info->FileAttributes, event.file_id, event.action));
	} else {
		PFILE_NOTIFY_INFORMATION info = (PFILE_NOTIFY_INFORMATION)&p_watch->buffer[event.offset];
		const int char_count = info->FileNameLength / sizeof(WCHAR);
		event.skip_current = char_count == 0;
		if (event.skip_current) {
			return;
		}

		const String &file_path = String::utf16((const char16_t *)info->FileName, char_count).replace_char('\\', '/');
		const String &full_path = p_watch->path + file_path;
		DWORD file_attr = GetFileAttributesW((LPCWSTR)(full_path.utf16().get_data()));
		if (file_attr == INVALID_FILE_ATTRIBUTES) {
			file_attr = 0;
		}
		_update_info(event, info->NextEntryOffset, p_watch->path, file_path, info->Action, file_attr);

		print_verbose(vformat("Event received for watch %d (\"%s\"): dir: \"%s\", name: \"%s\", attributes: %X, action: %X", p_watch->id, p_watch->path, event.dir_path, event.filename, file_attr, event.action));
	}
}

void FileWatcherIOCP::_process_event(DirWatchIOCP *p_watch, const EventInfo &p_event) {
	BitField<FileListener::FileTypeFlags> type = p_event.type;
	FileListener::FileAction action = FileListener::ACTION_ADD;

	StringBuilder old_path;

	switch (p_event.action) {
		case FILE_ACTION_ADDED:
		case FILE_ACTION_RENAMED_NEW_NAME: {
			if (p_watch->extended && p_event.file_id != -1) {
				HashMap<LONGLONG, PendingEvent> &pending_events = p_event.action == FILE_ACTION_ADDED ? deletions : moves;
				HashMap<LONGLONG, PendingEvent>::Iterator E = pending_events.find(p_event.file_id);
				if (E) {
					const PendingEvent &from = E->value;
					if (watches.has(from.id)) {
						action = FileListener::ACTION_MOVED;
						type = from.type;
						old_path += from.watch->path;
						old_path += from.filename;
						if (type.has_flag(FileListener::IS_DIR)) {
							old_path += "/";
						}

						_cancel_pending_deletion(p_event.dir_path, p_event.filename, p_event.file_id);
					} else {
						ERR_PRINT(vformat("The watch (id %d) was accidentally lost, but the ADD event for \"%s%s\" with file id %d was not cleared.", from.id, from.dir_path, from.filename, from.file_id));
					}
					pending_events.remove(E);
				}
			}
		} break;
		case FILE_ACTION_REMOVED:
		case FILE_ACTION_RENAMED_OLD_NAME: {
			if (p_watch->extended && p_event.file_id != -1) {
				HashMap<LONGLONG, PendingEvent> &pending_events = p_event.action == FILE_ACTION_REMOVED ? deletions : moves;
				HashMap<LONGLONG, PendingEvent>::Iterator E = pending_events.find(p_event.file_id);
				if (E) {
					const PendingEvent &from = E->value;
					if (watches.has(from.id)) {
						WARN_PRINT(vformat("An event with the same cookie already exists, wd: %d, dir path: \"%s\", file name: \"%s\".", from.id, from.watch->path, from.filename));
						from.watch->listener->file_action_handle(from.id, from.dir_path, from.filename, from.type, FileListener::ACTION_DELETE);
					} else {
						ERR_PRINT(vformat("An event with the same cookie %X for \"%s\" already exists, but the watch (wd %d) was accidentally lost.", from.file_id, from.filename, from.id));
					}
				}
				pending_events[p_event.file_id] = PendingEvent{ p_watch->id, p_watch, p_event.dir_path, p_event.filename, p_event.file_id, p_event.type, OS::get_singleton()->get_ticks_msec() };
				return;
			}
			action = FileListener::ACTION_DELETE;
		} break;
		case FILE_ACTION_MODIFIED: {
			action = FileListener::ACTION_MODIFIED;
		} break;
		default:
			break;
	}

	p_watch->listener->file_action_handle(p_watch->id, p_event.dir_path, p_event.filename, type, action, old_path.as_string());
}

void FileWatcherIOCP::_process_buffered_events(DirWatchIOCP *p_watch, DWORD p_total_bytes) {
	EventInfo event;
	event.has_next = p_total_bytes > 0;
	while (event.has_next) {
		_update_info_from_buffer(p_watch, event);

		if (event.skip_current) {
			continue;
		}

		if (p_watch->listener->file_event_filter(p_watch->id, event.dir_path, event.filename, event.type)) {
			continue; // Custom event filtering logic.
		}

		_process_event(p_watch, event);
	}

	_refresh_watch(p_watch, true);
}

bool FileWatcherIOCP::_run() {
	_flush_pending_watch_actions();

	DWORD num_of_bytes = 0;
	OVERLAPPED *ov = nullptr;
	ULONG_PTR comp_key = 0;

	BOOL res = GetQueuedCompletionStatus(iocp_handle, &num_of_bytes, &comp_key, &ov, QUERY_TIMEOUT_MS);

	if (!res) {
		_clear_pending_events(nullptr, true);
		_clear_pending_events(nullptr, false);

		DWORD err = GetLastError();
		if (err == WAIT_TIMEOUT) {
			return true;
		}

		ERR_FAIL_NULL_V_MSG(ov, false, vformat("GetQueuedCompletionStatus failed, error code: %d.", err));
		DirWatchIOCP *watch = (DirWatchIOCP *)comp_key;

		if (!watch->stop_now.is_set()) {
			ERR_PRINT(vformat("IOCP unexpected IO failure for watch %d, error code: %d, removing watch for \"%s\".", watch->id, err, watch->path));
			watch_ids.erase(watch->path);
			watches.erase(watch->id);
			if (watch->handle != INVALID_HANDLE_VALUE) {
				CloseHandle(watch->handle);
				watch->handle = INVALID_HANDLE_VALUE;
			}
		}
		memdelete(watch);
		return true;
	}

	if (comp_key == IOCP_SHUTDOWN_KEY) {
		return false;
	}

	if (comp_key == 0) {
		print_verbose(vformat("IOCP completion key is null, ov=%X", (ULONG_PTR)ov));
		return true;
	}

	DirWatchIOCP *watch = (DirWatchIOCP *)comp_key;
	if (watch->stop_now.is_set()) {
		memdelete(watch);
		return true;
	}

	if (num_of_bytes == 0) {
		// Missed file actions due to buffer overflowed.
		watch->listener->missing_actions_handle(watch->id, watch->path);
		_refresh_watch(watch, true);
		return true;
	}

	_process_buffered_events(watch, num_of_bytes);
	return true;
}

void FileWatcherIOCP::_stop_watch_internally() {
	PostQueuedCompletionStatus(iocp_handle, 0, IOCP_SHUTDOWN_KEY, nullptr);
}

bool FileWatcherIOCP::_refresh_watch(DirWatchIOCP *p_watch, bool p_clean_if_failed) {
	if (ready.is_set() && !p_watch->stop_now.is_set()) {
		if (p_watch->extended) {
			if (w10_ReadDirectoryChangesExW(p_watch->handle, p_watch->buffer.ptrw(),
						(DWORD)p_watch->buffer.size(), p_watch->recursive,
						p_watch->notify_filter, nullptr, &p_watch->overlapped,
						nullptr, ReadDirectoryNotifyExtendedInformation) != 0) {
				return true; // The OS and the file system support ReadDirectoryChangesExW.
			}

			DWORD err = GetLastError();
			if (ERROR_INVALID_FUNCTION == err) {
				p_watch->extended = false;
				WARN_PRINT(p_watch->type.has_flag(FileListener::IS_LINK)
								? vformat("The file system of \"%s\" (real: \"%s\") does not support ReadDirectoryChangesExW, error code: %d.", p_watch->path, p_watch->real_path, err)
								: vformat("The file system of \"%s\" does not support ReadDirectoryChangesExW, error code: %d.", p_watch->path, err));
			} else {
				WARN_PRINT(p_watch->type.has_flag(FileListener::IS_LINK)
								? vformat("Failed to call ReadDirectoryChangesExW for \"%s\" (real: \"%s\"), error code: %d.", p_watch->path, p_watch->real_path, err)
								: vformat("Failed to call ReadDirectoryChangesExW for \"%s\", error code: %d.", p_watch->path, err));
			}
		}

		if (ReadDirectoryChangesW(p_watch->handle, p_watch->buffer.ptrw(),
					(DWORD)p_watch->buffer.size(), p_watch->recursive,
					p_watch->notify_filter, nullptr,
					&p_watch->overlapped, nullptr) != 0) {
			return true;
		}

		DWORD err = GetLastError();
		ERR_PRINT(p_watch->type.has_flag(FileListener::IS_LINK)
						? vformat("Failed to call ReadDirectoryChangesW for \"%s\" (real: \"%s\"), error code: %d.", p_watch->path, p_watch->real_path, err)
						: vformat("Failed to call ReadDirectoryChangesW for \"%s\", error code: %d.", p_watch->path, err));
	}

	if (p_clean_if_failed) {
		print_verbose(vformat("Clean up unrefreshable watch (%d) for \"%s\" (real: \"%s\").", p_watch->id, p_watch->path, p_watch->real_path));

		_clear_pending_events(p_watch, true);
		_clear_pending_events(p_watch, false);

		p_watch->listener->missing_actions_handle(p_watch->id, p_watch->path);
		_remove_watch(p_watch->id);
	}

	return false;
}

WatchID FileWatcherIOCP::_create_watch(const String &p_dir_path, bool p_recursive, DirWatch **r_watch) {
	if (!ready.is_set()) {
		return UNAVAILABLE_WATCHER;
	}

	const HANDLE dir_handle = CreateFileW((LPCWSTR)p_dir_path.utf16().get_data(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr);

	if (dir_handle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		ERR_PRINT(vformat("Failed to create file handle for \"%s\", error code: %d", p_dir_path, err));
		return WATCH_FAILED;
	}

	DirWatchIOCP *watch = memnew(DirWatchIOCP);

	if (!CreateIoCompletionPort(dir_handle, iocp_handle, (ULONG_PTR)watch, 0)) {
		DWORD err = GetLastError();
		ERR_PRINT(vformat("Failed to bind the handle of \"%s\" to the I/O completion port, error code: %d.", p_dir_path, err));

		CloseHandle(dir_handle);
		memdelete(watch);
		return WATCH_FAILED;
	}

	watch->path = p_dir_path;
	watch->recursive = p_recursive;
	watch->handle = dir_handle;
	watch->buffer.resize(63 * 1024);
	watch->notify_filter = FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE;
	watch->stop_now.clear();
	watch->extended = w10_ReadDirectoryChangesExW != nullptr;

	if (!_refresh_watch(watch, false)) {
		CloseHandle(dir_handle);
		memdelete(watch);
		return WATCH_FAILED;
	}

	watch->id = last_watch_id++;
	watch->watcher = this;
	if (r_watch) {
		*r_watch = watch;
	}
	return watch->id;
}

void FileWatcherIOCP::_destroy_watch(DirWatchIOCP *p_watch) {
	ERR_FAIL_NULL(p_watch);
	p_watch->stop_now.set();
	CancelIoEx(p_watch->handle, &p_watch->overlapped);
	CloseHandle(p_watch->handle);
	p_watch->handle = INVALID_HANDLE_VALUE;
	if (is_watching()) {
		return; // We still need to wait for the cancelled IO completions to be dequeued.
	}
	memdelete(p_watch);
}

void FileWatcherIOCP::_remove_watch(WatchID p_id) {
	if (!ready.is_set()) {
		return;
	}

	HashMap<WatchID, DirWatch *>::Iterator I = watches.find(p_id);
	ERR_FAIL_NULL_MSG(I, vformat("The watch to be removed by %d does not exist.", p_id));
	DirWatchIOCP *watch = static_cast<DirWatchIOCP *>(I->value);
	ERR_FAIL_NULL_MSG(watch, vformat("The watch to be removed by %d is invalid.", p_id));

	print_verbose(vformat("Remove watch for \"%s\" with id: %d.", watch->path, watch->id));

	watch_ids.erase(watch->path);
	watches.erase(p_id);

	_clear_pending_events(watch, true);
	_clear_pending_events(watch, false);
	_destroy_watch(watch);
}

void FileWatcherIOCP::_clear_watches() {
	ERR_FAIL_COND_MSG(is_watching(), "Watcher is still running.");
	MutexLock lock(watch_actions_mutex);

	for (const KeyValue<WatchID, DirWatch *> &KV : watches) {
		_destroy_watch(static_cast<DirWatchIOCP *>(KV.value));
	}
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
			_destroy_watch(static_cast<DirWatchIOCP *>(E.watch));
		}
	}
	pending_watch_actions.clear();
}

Ref<FileWatcher> FileWatcherIOCP::_create_func() {
	return memnew(FileWatcherIOCP);
}

void FileWatcherIOCP::make_default() {
	_create = _create_func;

	supported_fs = {
		"NTFS",
		"ReFS",
	};
}

void FileWatcherIOCP::_init_win_api() {
	static bool inited = false;
	if (inited) {
		return;
	}
	inited = true;

	HMODULE kernel32 = GetModuleHandleW(L"Kernel32.dll");
	if (!kernel32) {
		return;
	}
	w10_ReadDirectoryChangesExW = (ReadDirectoryChangesExWPtr)(void *)GetProcAddress(kernel32, "ReadDirectoryChangesExW");
	if (!w10_ReadDirectoryChangesExW) {
		print_verbose("ReadDirectoryChangesExW is not supported on your OS; falling back to a lower-level alternative.");
	}
}

FileWatcherIOCP::FileWatcherIOCP() {
	_init_win_api();

	iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

	if (!iocp_handle) {
		DWORD err = GetLastError();
		ERR_PRINT(vformat("CreateIoCompletionPort failed, error code: %d", err));
		return;
	}

	ready.set();
}

FileWatcherIOCP::~FileWatcherIOCP() {
	if (!iocp_handle) {
		return;
	}

	stop_watch();

	ready.clear();

	_clear_watches();

	if (iocp_handle) {
		CloseHandle(iocp_handle);
		iocp_handle = nullptr;
	}
}

#endif // WINDOWS_ENABLED
