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

#include "scene/gui/button.h"
#include "scene/gui/scroll_container.h"

#include "tests/test_macros.h"

namespace TestScrollContainer {

TEST_CASE("[SceneTree][ScrollContainer] Follow focus") {
	ScrollContainer *sc = memnew(ScrollContainer);

	Control *ctrl = memnew(Control);

	Button *child_0 = memnew(Button);
	Button *child_1 = memnew(Button);
	Button *child_2 = memnew(Button);

	ctrl->add_child(child_0);
	ctrl->add_child(child_1);
	ctrl->add_child(child_2);
	sc->add_child(ctrl);
	SceneTree::get_singleton()->get_root()->add_child(sc);

	sc->set_custom_minimum_size(Size2(500, 500));
	ctrl->set_custom_minimum_size(Size2(1000, 1000));

	child_0->set_position(Point2());
	child_1->set_position(Point2(600, 600));
	child_2->set_position(Point2(30, 900));

	child_0->set_custom_minimum_size(Size2(10, 10));
	child_1->set_custom_minimum_size(Size2(10, 10));
	child_2->set_custom_minimum_size(Size2(10, 10));

	sc->set_follow_focus(true);

	SceneTree::get_singleton()->process(0.1f);

	SUBCASE("") {
		sc->set_h_scroll(50);
		CHECK_EQ(sc->get_h_scroll(), 50);
		SceneTree::get_singleton()->process(0.1f);
		child_1->grab_focus();
		REQUIRE_UNARY(child_1->has_focus());

		SceneTree::get_singleton()->process(0.1f);

		CHECK_EQ(sc->get_h_scroll(), 118);
		CHECK_EQ(sc->get_v_scroll(), 118);
	}

	memdelete(child_2);
	memdelete(child_1);
	memdelete(child_0);
	memdelete(ctrl);
	memdelete(sc);
}

} // namespace TestScrollContainer

#endif // TEST_SCROLL_CONTAINER_H
