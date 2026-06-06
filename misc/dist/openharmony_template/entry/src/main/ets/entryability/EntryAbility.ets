/**************************************************************************/
/*  EntryAbility.ets                                                      */
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

import { AbilityConstant, ConfigurationConstant, UIAbility, Want } from "@kit.AbilityKit";
import { hilog } from "@kit.PerformanceAnalysisKit";
import { window } from "@kit.ArkUI";
import { abilityAccessCtrl, Permissions } from "@kit.AbilityKit";
import plugin from "libgodot.so";

const DOMAIN = 0x0000;
const TAG = "testTag";

export default class EntryAbility extends UIAbility {
  private initialized: boolean = false;
  private started: boolean = false;

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onCreate");

    try {
      let resId = $r("app.string.user_permissions").id;
      let user_permissions = this.context.resourceManager.getStringSync(resId);
      let user_permissions_list: Array<Permissions> = [];
      user_permissions.split(",").forEach((value) => {
        user_permissions_list.push(value.trim() as Permissions);
      });
      if (user_permissions_list.length > 0) {
        let atManager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
        atManager.requestPermissionsFromUser(this.context, user_permissions_list);
      }
    } catch (err) {
      hilog.error(DOMAIN, TAG, "Permission error: %{public}s", JSON.stringify(err));
    }

    try {
      let appContext = this.context.getApplicationContext();
      appContext.setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_NOT_SET);
    } catch (err) {
      hilog.error(DOMAIN, TAG, "Set color mode error: %{public}s", JSON.stringify(err));
    }

    let args = want.parameters?.godot_args as string;
    this.initialized = plugin.initialize(this.context, this.context.resourceManager, args ?? "");
    hilog.info(DOMAIN, TAG, "%{public}s", "Godot initialized.");
  }

  onNewWant(want: Want, launchParam: AbilityConstant.LaunchParam): void {}

  onDestroy(): void {
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onDestroy");

    if (this.initialized) {
      plugin.finalize();
      this.initialized = false;
      hilog.info(DOMAIN, TAG, "%{public}s", "Godot finalized.");
    }
  }

  async onWindowStageCreate(windowStage: window.WindowStage): Promise<void> {
    // Main window is created, set main page for this ability.
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onWindowStageCreate");

    let mainWindow: window.Window | null = null;

    try {
      mainWindow = await windowStage.getMainWindow();
    } catch (err) {
      hilog.error(DOMAIN, TAG, "getMainWindow failed: %{public}s", JSON.stringify(err));
      return;
    }

    if (!mainWindow) {
      hilog.error(DOMAIN, TAG, "mainWindow is null");
      return;
    }

    // The navigation bar cannot be shown or hidden independently.
    try {
      mainWindow.setWindowSystemBarEnable([]);
    } catch (err) {
      hilog.error(DOMAIN, TAG, "setWindowSystemBarEnable failed: %{public}s", JSON.stringify(err));
    }

    let windowID: number = -1;
    try {
      windowID = mainWindow.getWindowProperties().id;
      plugin.setMainWindowId(windowID);
      AppStorage.setOrCreate<number>("windowID", windowID);
      hilog.info(DOMAIN, TAG, "Window ID: %{public}d", windowID);
    } catch (err) {
      hilog.error(DOMAIN, TAG, "getWindowProperties failed: %{public}s", JSON.stringify(err));
    }

    windowStage.loadContent("pages/Index", (err) => {
      if (err.code) {
        const templ = "Failed to load the content. Cause: %{public}s";
        hilog.error(DOMAIN, TAG, templ, JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, TAG, "Succeeded in loading the content.");
      this.started = plugin.is_started();
    });
  }

  onWindowStageDestroy(): void {
    // Main window is destroyed, release UI related resources
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onWindowStageDestroy");

    if (this.started) {
      plugin.stop();
      this.started = false;
      hilog.info(DOMAIN, TAG, "%{public}s", "Godot stopped.");
    }
  }

  onForeground(): void {
    // Ability has brought to foreground
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onForeground");

    if (this.started) {
      plugin.resume();
      plugin.focus_in();
      hilog.info(DOMAIN, TAG, "%{public}s", "Godot resumed.");
    }
  }

  onBackground(): void {
    // Ability has back to background
    hilog.info(DOMAIN, TAG, "%{public}s", "Ability onBackground");

    if (this.started) {
      plugin.focus_out();
      plugin.pause();
      hilog.info(DOMAIN, TAG, "%{public}s", "Godot paused.");
    }
  }
}
