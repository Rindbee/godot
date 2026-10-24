/**************************************************************************/
/*  AbilityStage.ets                                                      */
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

import { AbilityStage, Want } from "@kit.AbilityKit";
import { HashMap } from "@kit.ArkTS";

export default class GodotAbilityStage extends AbilityStage {
  private project_ids: HashMap<string, number> = new HashMap();
  private id_counter = 0;

  private parse_id(want: Want): number {
    let project_id = want.parameters?.godot_project_id as string;
    if (!project_id) {
      project_id = "ProjectManager";
    }
    let id = 0;
    try {
      if (this.project_ids.hasKey(project_id)) {
        id = this.project_ids.get(project_id);
      } else {
        id = this.id_counter++;
        this.project_ids.set(project_id, id);
      }
    } catch (error) {}
    return id;
  }

  onAcceptWant(want: Want): string {
    let id = this.parse_id(want);
    return `Godot${id}`;
  }
  async onAcceptWantAsync(want: Want): Promise<string> {
    let id = this.parse_id(want);
    return `Godot${id}`;
  }

  onNewProcessRequest(want: Want): string {
    let id = this.parse_id(want);
    return `GodotProcess${id}`;
  }
  async onNewProcessRequestAsync(want: Want): Promise<string> {
    let id = this.parse_id(want);
    return `GodotProcess${id}`;
  }
}
