/**************************************************************************/
/*  AbilityUtils.ets                                                      */
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

import { common, Want, StartOptions } from "@kit.AbilityKit";
import { hilog } from "@kit.PerformanceAnalysisKit";

const DOMAIN = 0x0000;
const TAG = "testTag";

export function startAbility(
  context: common.UIAbilityContext,
  project_id: string,
  arg_str: string,
): boolean {
  if (context === undefined || context === null) {
    hilog.error(DOMAIN, TAG, "startAbility failed: context is null.");
    return false;
  }

  let want: Want = {
    deviceId: "",
    bundleName: context.abilityInfo.bundleName,
    moduleName: context.abilityInfo.moduleName,
    abilityName: context.abilityInfo.name,
    parameters: {
      godot_project_id: project_id,
      godot_args: arg_str,
    },
  };
  let options: StartOptions = {
    displayId: context.config.displayId,
  };

  let succeed = true;
  try {
    context.startAbility(want, options, (err: BusinessError) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, "startAbility failed: %{public}s.", JSON.stringify(err));
        succeed = false;
        return;
      }
      hilog.info(DOMAIN, TAG, "startAbility succeed.");
    });
  } catch (err) {
    hilog.error(DOMAIN, TAG, "startAbility failed: %{public}s.", JSON.stringify(err));
    succeed = false;
  }

  return succeed;
}

export function terminateSelf(context: common.UIAbilityContext): boolean {
  if (context === undefined || context === null) {
    hilog.error(DOMAIN, TAG, "terminateSelf failed: context is null.");
    return false;
  }

  let succeed = true;
  try {
    context.terminateSelf((err: BusinessError) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, "terminateSelf failed: %{public}s.", JSON.stringify(err));
        succeed = false;
        return;
      }
      hilog.info(DOMAIN, TAG, "terminateSelf succeed.");
    });
  } catch (err) {
    hilog.error(DOMAIN, TAG, "terminateSelf failed: %{public}s.", JSON.stringify(err));
    succeed = false;
  }

  return succeed;
}
