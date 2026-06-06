from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

from methods import print_error, print_info, print_warning
from platform_methods import validate_arch

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment


def get_name():
    return "OpenHarmony"


def can_build():
    return True


def get_tools(env: SConsEnvironment):
    return ["clang", "clang++", "as", "ar", "link"]


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        ("OPENHARMONY_SDK_PATH", "Path to the OpenHarmony SDK", get_default_sdk_path()),
        (
            "OPENHARMONY_CMD_LINE_TOOL_PATH",
            "Path to the OpenHarmony command line tools",
            get_default_cmd_line_tools_path(),
        ),
        BoolVariable("generate_bundle", "Generate an APP bundle after building OpenHarmony binaries", True),
        # BoolVariable("oh_audio_advanced", "Allow to obtain the sandbox paths of the user files.", True),
        BoolVariable("oh_environment", "Allow to obtain the sandbox paths of the user files.", True),
    ]


def get_doc_classes():
    return [
        "EditorExportPlatformOpenHarmony",
    ]


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_debug",
        "builtin_pcre2_with_jit": False,
        "opengl3": False,
        "library_type": "shared_library",
        "supported": ["library"],
    }


def get_default_sdk_path():
    # Obtain from environment variables.
    return os.environ.get("OPENHARMONY_SDK_PATH")


def get_default_cmd_line_tools_path():
    # Obtain from environment variables.
    return os.environ.get("OPENHARMONY_CMD_LINE_TOOL_PATH")


def get_min_target_api() -> int:
    return 18


def get_ndk_api_version(sdk_root: str) -> int:
    # Detect OpenHarmony NDK API Level from sdk_root/native/oh-uni-package.json.
    json_path = os.path.join(sdk_root, "native", "oh-uni-package.json")

    if os.path.exists(json_path):
        try:
            with open(json_path, "r") as f:
                data = json.load(f)
                api_version = data.get("apiVersion")
                if api_version:
                    return int(api_version)
        except (json.JSONDecodeError, ValueError, OSError):
            return -1

    return -1


@dataclass
class ModuleConfig:
    name: str
    min_api: int
    required: bool
    libname: Optional[str]
    define: Optional[str]
    enable: bool

    def is_compatible(self, api_version: int) -> bool:
        return api_version >= self.min_api


def check_module(env: SConsEnvironment, module: ModuleConfig, api_version: int, min_comp_api_level: int) -> int:
    if not module.required and not module.enable:
        return min_comp_api_level

    if module.is_compatible(api_version):
        if module.libname:
            env.Append(LIBS=[module.libname])
        if module.define:
            env.Append(CPPDEFINES=[module.define])

        if module.min_api > min_comp_api_level:
            return module.min_api
        return min_comp_api_level

    print_warning(
        f"This module {module.name} will be automatically disabled as the api version {api_version} is lower than expected {module.min_api}."
    )

    if module.required:
        print_error(f"Exiting as this module {module.name} is required.")
        sys.exit(255)

    return min_comp_api_level


def configure(env: SConsEnvironment):
    # Validate arch.
    supported_arches = ["arm64", "x86_64"]
    validate_arch(env["arch"], get_name(), supported_arches)

    # Check SDK path.
    sdk_root = env["OPENHARMONY_SDK_PATH"]
    if (sdk_root == "") or (not os.path.exists(sdk_root)):
        print_error("OpenHarmony SDK not found. Please set OPENHARMONY_SDK_PATH to the SDK path.")
        sys.exit(255)

    # Validate NDK API level.
    api_version = get_ndk_api_version(sdk_root)
    if api_version == -1:
        print_error(
            "Could not detect OpenHarmony NDK API version from oh-uni-package.json. "
            "Please ensure the SDK path points to a valid OpenHarmony SDK."
        )
    elif api_version < get_min_target_api():
        print_error(
            f"OpenHarmony NDK API version {api_version} is not supported. "
            f"Minimum required is API {get_min_target_api()}."
        )
        sys.exit(255)

    # Architecture
    if env["arch"] == "x86_64":
        target_name = "x86_64-linux-ohos"
        env.Append(ASFLAGS=["-arch", "x86_64"])
    elif env["arch"] == "arm64":
        target_name = "aarch64-linux-ohos"
        env.Append(ASFLAGS=["-arch", "aarch64"])

    env.Append(CCFLAGS=[f"--target={target_name}"])
    env.Append(LINKFLAGS=[f"--target={target_name}"])

    # Sysroot.
    sysroot_path = os.path.join(sdk_root, "native", "sysroot")
    env.Append(CCFLAGS=[f"--sysroot={sysroot_path}"])
    env.Append(LINKFLAGS=[f"--sysroot={sysroot_path}"])

    # LTO

    if env["lto"] == "auto":  # Enable LTO for production.
        env["lto"] = "thin"

    if env["lto"] != "none":
        if env["lto"] == "thin":
            env.Append(CCFLAGS=["-flto=thin"])
            env.Append(LINKFLAGS=["-flto=thin"])
        else:
            env.Append(CCFLAGS=["-flto"])
            env.Append(LINKFLAGS=["-flto"])

    # Compiler configuration

    compiler_path = os.path.join(sdk_root, "native", "llvm", "bin")

    # To obtain more detailed compiler paths when compiledb is enabled.
    env["CC"] = os.path.join(compiler_path, "clang")
    env["CXX"] = os.path.join(compiler_path, "clang++")
    env["S_compiler"] = os.path.join(compiler_path, "clang")
    env["AR"] = os.path.join(compiler_path, "llvm-ar")
    env["AS"] = os.path.join(compiler_path, "llvm-as")
    env["LINK"] = os.path.join(compiler_path, "clang++")
    env["SHLINK"] = os.path.join(compiler_path, "clang++")
    env["RANLIB"] = os.path.join(compiler_path, "llvm-ranlib")

    env.Append(
        CPPPATH=[
            "#platform/openharmony",
            f"{sdk_root}/native/llvm/lib/clang/15.0.4/include",
            f"{sdk_root}/native/llvm/include/libcxx-ohos/include/c++/v1",
            f"{sysroot_path}/usr/include/{target_name}",
            f"{sysroot_path}/usr/include",
        ]
    )

    env.Append(CCFLAGS=["-fPIC", "-fvisibility=hidden"])

    env.Append(CPPDEFINES=["OPENHARMONY_ENABLED", "UNIX_ENABLED", "__OPEN_HARMONY__", "MBEDTLS_NO_UDBL_DIVISION"])

    env.Append(LINKFLAGS=["-fuse-ld=lld"])
    env.Append(LINKFLAGS=["-Wl,--build-id"])

    env["SHLIBSUFFIX"] = ".so"
    env.Append(SHLINKFLAGS=["-Wl,-soname,libgodot.so"])

    # System libs.
    env.Append(LIBPATH=[f"{sdk_root}/native/llvm/lib/{target_name}"])
    env.Append(LIBPATH=[f"{sysroot_path}/usr/lib/{target_name}"])

    min_comp_api_level = 0

    modules = [
        ModuleConfig("NAPI", 12, True, "ace_napi.z", None, True),
        ModuleConfig("OH_LOG", 8, True, "hilog_ndk.z", None, True),
        ModuleConfig("ArkUI NDK", 20, True, "ace_ndk.z", None, True),
        ModuleConfig("DeviceInfo", 10, True, "deviceinfo_ndk.z", None, True),
        ModuleConfig("OH_AT", 12, True, "ability_access_control", None, True),
        ModuleConfig("OH_AbilityRuntime_Want", 17, True, "ability_base_want", None, True),
        ModuleConfig("OH_AbilityRuntime", 16, True, "ability_runtime", None, True),
        ModuleConfig("OH_NativeDisplayManager", 14, True, "native_display_manager", None, True),
        ModuleConfig("OH_Drawing", 14, True, "native_drawing", None, True),
        ModuleConfig("OH_NativeVSync", 9, True, "native_vsync", None, True),
        ModuleConfig("OH_NativeWindow", 12, True, "native_window", None, True),
        ModuleConfig("OH_WindowManager", 15, True, "native_window_manager", None, True),
        ModuleConfig("OH_Pixelmap", 22, True, "pixelmap", None, True),
        ModuleConfig("OH_Audio", 20, True, "ohaudio", None, True),
        ModuleConfig("OH_Input", 22, True, "ohinput", None, True),
        ModuleConfig("OH_InputMethod", 12, True, "ohinputmethod", None, True),
        ModuleConfig("OH_Pasteboard", 13, True, "pasteboard", None, True),
        ModuleConfig("OH_ResourceManager", 12, True, "rawfile.z", None, True),
        ModuleConfig("OH_Udmf", 13, True, "udmf", None, True),
        ModuleConfig("OH_Environment", 12, False, "ohenvironment", None, True),
    ]

    for module in modules:
        min_comp_api_level = check_module(env, module, api_version, min_comp_api_level)

    if not env["builtin_zlib"]:
        env.Append(LIBS=["z"])

    if not env["builtin_icu4c"]:
        env.Append(LIBS=["icu"])
        print_warning("The icu4c functionality provided by the system may be incomplete.")

    if env["vulkan"]:
        env.Append(CPPDEFINES=["VULKAN_ENABLED", "RD_ENABLED"])
        if not env["use_volk"]:
            env.Append(LIBS=["vulkan"])

    if env["opengl3"]:
        print_error("opengl3 is not support on OpenHarmony")
        sys.exit(255)

    print_info(f"The minimum compatible API version for this build is {min_comp_api_level}.")
