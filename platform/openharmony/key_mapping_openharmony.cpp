/**************************************************************************/
/*  key_mapping_openharmony.cpp                                           */
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

#include "key_mapping_openharmony.h"

void KeyMappingOpenHarmony::initialize() {
	// KEY_0-KEY_9 (2000-2009)
	for (int32_t i = 0; i < 10; i++) {
		keysym_maps[i + 2000] = static_cast<Key>(static_cast<int>(Key::KEY_0) + i);
	}

	// A-Z (2017-2042)
	for (int32_t i = 0; i < 26; i++) {
		keysym_maps[i + 2017] = static_cast<Key>(static_cast<int>(Key::A) + i);
	}

	// F1-F12 (2090-2101)
	for (int32_t i = 0; i < 12; i++) {
		keysym_maps[i + 2090] = static_cast<Key>(static_cast<int>(Key::F1) + i);
	}

	// KP_0-KP_9 (2103-2112)
	for (int32_t i = 0; i < 10; i++) {
		keysym_maps[i + 2103] = static_cast<Key>(static_cast<int>(Key::KP_0) + i);
	}

	// F13-F24 (2816-2827)
	for (int32_t i = 0; i < 12; i++) {
		keysym_maps[i + 2816] = static_cast<Key>(static_cast<int>(Key::F13) + i);
	}

	keysym_maps[2] = Key::BACK;
	keysym_maps[9] = Key::SEARCH;
	keysym_maps[10] = Key::MEDIAPLAY; // MEDIA_PLAY_PAUSE.
	keysym_maps[11] = Key::MEDIASTOP;
	keysym_maps[12] = Key::MEDIANEXT;
	keysym_maps[13] = Key::MEDIAPREVIOUS;

	keysym_maps[16] = Key::VOLUMEUP;
	keysym_maps[17] = Key::VOLUMEDOWN;
	keysym_maps[22] = Key::VOLUMEMUTE; // Speaker Mute key.
	keysym_maps[23] = Key::VOLUMEMUTE; // Mute key.

	keysym_maps[2010] = Key::ASTERISK;
	keysym_maps[2011] = Key::NUMBERSIGN;

	// Dpad keys.
	keysym_maps[2012] = Key::UP;
	keysym_maps[2013] = Key::DOWN;
	keysym_maps[2014] = Key::LEFT;
	keysym_maps[2015] = Key::RIGHT;
	keysym_maps[2016] = Key::ENTER; // DPAD_CENTER.

	keysym_maps[2043] = Key::COMMA;
	keysym_maps[2044] = Key::PERIOD;
	keysym_maps[2045] = Key::ALT;
	keysym_maps[2046] = Key::ALT;
	keysym_maps[2047] = Key::SHIFT;
	keysym_maps[2048] = Key::SHIFT;
	keysym_maps[2049] = Key::TAB;
	keysym_maps[2050] = Key::SPACE;

	keysym_maps[2053] = Key::LAUNCHMAIL;
	keysym_maps[2054] = Key::ENTER;
	keysym_maps[2055] = Key::BACKSPACE;
	keysym_maps[2056] = Key::QUOTELEFT;
	keysym_maps[2057] = Key::MINUS;
	keysym_maps[2058] = Key::EQUAL;
	keysym_maps[2059] = Key::BRACKETLEFT;
	keysym_maps[2060] = Key::BRACKETRIGHT;
	keysym_maps[2061] = Key::BACKSLASH;
	keysym_maps[2062] = Key::SEMICOLON;
	keysym_maps[2063] = Key::APOSTROPHE;
	keysym_maps[2064] = Key::SLASH;
	keysym_maps[2065] = Key::AT;
	keysym_maps[2066] = Key::PLUS;
	keysym_maps[2067] = Key::MENU;
	keysym_maps[2068] = Key::PAGEUP;
	keysym_maps[2069] = Key::PAGEDOWN;
	keysym_maps[2070] = Key::ESCAPE;
	keysym_maps[2071] = Key::KEY_DELETE;
	keysym_maps[2072] = Key::CTRL;
	keysym_maps[2073] = Key::CTRL;
	keysym_maps[2074] = Key::CAPSLOCK;
	keysym_maps[2075] = Key::SCROLLLOCK;
	keysym_maps[2076] = Key::META;
	keysym_maps[2077] = Key::META;
	keysym_maps[2079] = Key::SYSREQ;
	keysym_maps[2080] = Key::PAUSE; // Break/Pause key.
	keysym_maps[2081] = Key::HOME;
	keysym_maps[2082] = Key::END;
	keysym_maps[2083] = Key::INSERT;
	keysym_maps[2084] = Key::FORWARD;
	keysym_maps[2085] = Key::MEDIAPLAY;

	keysym_maps[2089] = Key::MEDIARECORD;

	// Numeric keypad kyes.
	keysym_maps[2102] = Key::NUMLOCK;
	keysym_maps[2113] = Key::KP_DIVIDE;
	keysym_maps[2114] = Key::KP_MULTIPLY;
	keysym_maps[2115] = Key::KP_SUBTRACT;
	keysym_maps[2116] = Key::KP_ADD;
	keysym_maps[2117] = Key::KP_PERIOD;
	keysym_maps[2118] = Key::COMMA; // Numeric keypad kyes.
	keysym_maps[2119] = Key::KP_ENTER;
	keysym_maps[2120] = Key::EQUAL; // Numeric keypad kyes.
	keysym_maps[2121] = Key::PARENLEFT;
	keysym_maps[2122] = Key::PARENRIGHT;

	keysym_maps[2669] = Key::KEYBOARD;

	for (const KeyValue<int32_t, Key> &E : keysym_maps) {
		keysym_map_inv[E.value] = E.key;
	}

	keysym_map_inv[Key::MEDIAPLAY] = 2085;
	keysym_map_inv[Key::VOLUMEMUTE] = 23;
	keysym_map_inv[Key::ENTER] = 2054;
	keysym_map_inv[Key::ALT] = 2045;
	keysym_map_inv[Key::SHIFT] = 2047;
	keysym_map_inv[Key::CTRL] = 2072;
	keysym_map_inv[Key::META] = 2076;
	keysym_map_inv[Key::COMMA] = 2043;
	keysym_map_inv[Key::EQUAL] = 2058;

	// ALT keys.
	location_map[2045] = KeyLocation::LEFT;
	location_map[2046] = KeyLocation::RIGHT;
	// SHIFT keys.
	location_map[2047] = KeyLocation::LEFT;
	location_map[2048] = KeyLocation::RIGHT;
	// CTRL keys.
	location_map[2072] = KeyLocation::LEFT;
	location_map[2073] = KeyLocation::RIGHT;
	// META keys.
	location_map[2076] = KeyLocation::LEFT;
	location_map[2077] = KeyLocation::RIGHT;
}

bool KeyMappingOpenHarmony::is_sym_numpad(int32_t p_keysym) {
	return p_keysym >= 2103 && p_keysym <= 2122;
}

Key KeyMappingOpenHarmony::map_key(int32_t p_keysym) {
	const Key *key = keysym_maps.getptr(p_keysym);
	if (key) {
		return *key;
	}
	return Key::NONE;
}

int32_t KeyMappingOpenHarmony::unmap_key(Key p_key) {
	const int32_t *key = keysym_map_inv.getptr(p_key);
	if (key) {
		return *key;
	}
	return -1; // Unknown key.
}

KeyLocation KeyMappingOpenHarmony::get_location(int32_t p_keysym) {
	const KeyLocation *location = location_map.getptr(p_keysym);
	if (location) {
		return *location;
	}
	return KeyLocation::UNSPECIFIED;
}
