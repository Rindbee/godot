/**************************************************************************/
/*  test_scroll_container.h                                               */
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

#ifndef TEST_SCROLL_CONTAINER_H
#define TEST_SCROLL_CONTAINER_H

#include "scene/gui/control.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/scroll_container.h"

#include "tests/test_macros.h"

namespace TestScrollContainer {

TEST_CASE("[SceneTree][ScrollContainer] Follow focus") {
	ScrollContainer *sc = memnew(ScrollContainer);
	sc->set_custom_minimum_size(Size2(100, 100));
	SceneTree::get_singleton()->get_root()->add_child(sc);

	Control *ctrl = memnew(Control);
	ctrl->set_custom_minimum_size(Size2(1000, 1000));
	sc->add_child(ctrl);

	Control *child_0 = memnew(Control);
	child_0->set_focus_mode(Control::FOCUS_ALL);
	child_0->set_position(Point2(250, 250));
	child_0->set_custom_minimum_size(Size2(10, 10));
	ctrl->add_child(child_0);

	SceneTree::get_singleton()->process(0.1f); // Wait one frame for autoconfiguration to complete.

	REQUIRE_UNARY_FALSE(sc->is_following_focus());
	REQUIRE_EQ(sc->get_h_scroll(), 0);
	REQUIRE_EQ(sc->get_v_scroll(), 0);

	SUBCASE("No scrolling when focused by default") {
		REQUIRE_UNARY_FALSE(child_0->has_focus());
		child_0->grab_focus();
		REQUIRE_UNARY(child_0->has_focus());

		CHECK_EQ(sc->get_h_scroll(), 0);
		CHECK_EQ(sc->get_v_scroll(), 0);
	}

	SUBCASE("Enable follow focus") {
		sc->set_follow_focus(true);
		REQUIRE_UNARY(sc->is_following_focus());

		SUBCASE("Scroll when focused") {
			REQUIRE_UNARY_FALSE(child_0->has_focus());
			child_0->grab_focus();
			REQUIRE_UNARY(child_0->has_focus());

			CHECK_EQ(sc->get_h_scroll(), 168);
			CHECK_EQ(sc->get_v_scroll(), 168);

			SUBCASE("Not scroll when already focused") {
				sc->set_h_scroll(0);
				sc->set_v_scroll(0);
				REQUIRE_EQ(sc->get_h_scroll(), 0);
				REQUIRE_EQ(sc->get_v_scroll(), 0);

				REQUIRE_UNARY(child_0->has_focus());
				child_0->grab_focus();
				REQUIRE_UNARY(child_0->has_focus());

				CHECK_EQ(sc->get_h_scroll(), 0);
				CHECK_EQ(sc->get_v_scroll(), 0);
			}
		}

		SUBCASE("The control is already fully visible, not scroll when focused") {
			sc->set_h_scroll(240);
			sc->set_v_scroll(240);

			SceneTree::get_singleton()->process(0.1f); // Wait one frame for autoconfiguration to complete.

			REQUIRE_EQ(sc->get_h_scroll(), 240);
			REQUIRE_EQ(sc->get_v_scroll(), 240);

			REQUIRE_UNARY_FALSE(child_0->has_focus());
			child_0->grab_focus();
			REQUIRE_UNARY(child_0->has_focus());

			CHECK_EQ(sc->get_h_scroll(), 240);
			CHECK_EQ(sc->get_v_scroll(), 240);
		}
	}

	memdelete(child_0);
	memdelete(ctrl);
	memdelete(sc);
}

} // namespace TestScrollContainer

#endif // TEST_SCROLL_CONTAINER_H
