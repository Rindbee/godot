/**************************************************************************/
/*  efsw_watcher.cpp                                                      */
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

#include "efsw_watcher.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"

class EFSWListenerProxy : public efsw::FileWatchListener {
private:
	EFSWListener *target = nullptr;

public:
	void handleFileAction(efsw::WatchID p_watchid, const std::string &p_dir, const std::string &p_filename, bool p_is_dir, efsw::Actions::Action p_action, std::string p_old_filename) override {
		if (target) {
			target->_file_action_handle(
					p_watchid,
					String(p_dir.c_str()),
					String(p_filename.c_str()),
					p_is_dir,
					static_cast<EFSWListener::FileAction>(p_action),
					String(p_old_filename.c_str()));
		}
	}

	void handleMissedFileActions(efsw::WatchID p_watchid, const std::string &p_dir) override {
		if (target) {
			target->_missed_file_actions_handle(p_watchid, String(p_dir.c_str()));
		}
	}

	EFSWListenerProxy(EFSWListener *p_target) :
			target(p_target) {}
};

void EFSWListener::_bind_methods() {
	GDVIRTUAL_BIND(_file_action_handle, "watch_id", "directory", "filename", "is_dir", "action", "old_filename");
	GDVIRTUAL_BIND(_missed_file_actions_handle, "watch_id", "directory");

	BIND_ENUM_CONSTANT(ACTION_ADD);
	BIND_ENUM_CONSTANT(ACTION_DELETE);
	BIND_ENUM_CONSTANT(ACTION_MODIFIED);
	BIND_ENUM_CONSTANT(ACTION_MOVED);
}

void EFSWListener::_file_action_handle(int p_watch_id, const String &p_dir, const String &p_filename, bool p_is_dir, FileAction p_action, const String &p_old_filename) {
	GDVIRTUAL_CALL(_file_action_handle, p_watch_id, p_dir, p_filename, p_is_dir, p_action, p_old_filename);
}

void EFSWListener::_missed_file_actions_handle(int p_watch_id, const String &p_dir) {
	GDVIRTUAL_CALL(_missed_file_actions_handle, p_watch_id, p_dir);
}

EFSWListener::EFSWListener() {
	proxy = new EFSWListenerProxy(this);
}

EFSWListener::~EFSWListener() {
	ERR_FAIL_NULL(proxy);
	delete proxy;
	proxy = nullptr;
}

void EFSWWatcher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_watch", "directory", "listener", "recursive"), &EFSWWatcher::add_watch, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_watch_directories"), &EFSWWatcher::get_watch_directories);
	ClassDB::bind_method(D_METHOD("remove_watch_by_id", "watch_id"), &EFSWWatcher::remove_watch_by_id);
	ClassDB::bind_method(D_METHOD("remove_watch_by_path", "directory"), &EFSWWatcher::remove_watch_by_path);
	ClassDB::bind_method(D_METHOD("watch"), &EFSWWatcher::watch);

	ClassDB::bind_method(D_METHOD("set_allow_out_of_scope_links", "enable"), &EFSWWatcher::set_allow_out_of_scope_links);
	ClassDB::bind_method(D_METHOD("get_allow_out_of_scope_links"), &EFSWWatcher::get_allow_out_of_scope_links);
	ClassDB::bind_method(D_METHOD("set_follow_symlinks", "enable"), &EFSWWatcher::set_follow_symlinks);
	ClassDB::bind_method(D_METHOD("get_follow_symlinks"), &EFSWWatcher::get_follow_symlinks);
	ClassDB::bind_method(D_METHOD("set_force_generic", "enable"), &EFSWWatcher::set_force_generic);
	ClassDB::bind_method(D_METHOD("get_force_generic"), &EFSWWatcher::get_force_generic);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_out_of_scope_links"), "set_allow_out_of_scope_links", "get_allow_out_of_scope_links");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_symlinks"), "set_follow_symlinks", "get_follow_symlinks");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "force_generic"), "set_force_generic", "get_force_generic");

	BIND_ENUM_CONSTANT(NO_ERROR);
	BIND_ENUM_CONSTANT(FILE_NOT_FOUND);
	BIND_ENUM_CONSTANT(FILE_REPEATED);
	BIND_ENUM_CONSTANT(FILE_OUT_OF_SCOPE);
	BIND_ENUM_CONSTANT(FILE_NOT_READABLE);
	BIND_ENUM_CONSTANT(FILE_REMOTE);
	BIND_ENUM_CONSTANT(WATCHER_FAILED);
	BIND_ENUM_CONSTANT(UNSPECIFIED);
}

int EFSWWatcher::add_watch(const String &p_dir_path, const EFSWListener *p_listener, bool p_recursive) {
	ERR_FAIL_NULL_V(proxy, -1);
	ERR_FAIL_NULL_V(p_listener, -1);

	const String &global_path = ProjectSettings::get_singleton()->globalize_path(p_dir_path);
	int watch_id = -1;
	watch_id = proxy->addWatch(global_path.utf8().get_data(), p_listener->get_proxy(), p_recursive);
	return watch_id;
}

TypedArray<String> EFSWWatcher::get_watch_directories() const {
	std::vector<std::string> dirs = proxy->directories();

	TypedArray<String> watch_dirs;
	watch_dirs.resize(dirs.size());

	for (const std::string &dir : dirs) {
		watch_dirs.push_back(String::utf8(dir.c_str()));
	}

	return watch_dirs;
}

void EFSWWatcher::remove_watch_by_id(int p_watch_id) {
	ERR_FAIL_NULL(proxy);
	proxy->removeWatch(p_watch_id);
}

void EFSWWatcher::remove_watch_by_path(const String &p_dir_path) {
	ERR_FAIL_NULL(proxy);
	const String &global_path = ProjectSettings::get_singleton()->globalize_path(p_dir_path);
	proxy->removeWatch(global_path.utf8().get_data());
}

void EFSWWatcher::watch() {
	ERR_FAIL_NULL(proxy);
	proxy->watch();
}

void EFSWWatcher::set_follow_symlinks(bool p_enable) {
	proxy->followSymlinks(p_enable);
}

bool EFSWWatcher::get_follow_symlinks() const {
	return proxy->followSymlinks();
}

void EFSWWatcher::set_allow_out_of_scope_links(bool p_enable) {
	proxy->allowOutOfScopeLinks(p_enable);
}

bool EFSWWatcher::get_allow_out_of_scope_links() const {
	return proxy->allowOutOfScopeLinks();
}

void EFSWWatcher::set_force_generic(bool p_force) {
	if (force_generic == p_force) {
		return;
	}
	ERR_FAIL_NULL(proxy);
	delete proxy;

	force_generic = p_force;
	proxy = new efsw::FileWatcher(force_generic);
}

EFSWWatcher::EFSWWatcher() {
	proxy = new efsw::FileWatcher(force_generic);
}

EFSWWatcher::~EFSWWatcher() {
	ERR_FAIL_NULL(proxy);
	delete proxy;
	proxy = nullptr;
}
