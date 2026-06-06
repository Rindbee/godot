"""Functions used to generate source files during build time"""

import os
import shutil
import subprocess
import sys

from methods import print_error


def setup_hvigor_env(cmd_line_tools_path):
    os_env = os.environ.copy()

    java_path = shutil.which("java")
    if not java_path:
        java_home = os.environ.get("JAVA_HOME")
        if java_home and os.path.isfile(os.path.join(java_home, "bin", "java")):
            java_bin = os.path.join(java_home, "bin")
            os_env["PATH"] = java_bin + os.pathsep + os_env.get("PATH", "")
        else:
            print_error("Neither JAVA_HOME nor 'java' command found.")
            sys.exit(255)

    hvigorw_path = shutil.which("hvigorw")
    if not hvigorw_path:
        hvigor_bin = os.path.join(cmd_line_tools_path, "bin")
        if os.path.isdir(hvigor_bin):
            os_env["PATH"] = hvigor_bin + os.pathsep + os_env["PATH"]
        else:
            print_error("Neither OPENHARMONY_CMD_LINE_TOOL_PATH nor 'hvigorw' command found.")
            sys.exit(255)

    return os_env


def build_bundle(env, os_env, app_dir):
    original_dir = os.getcwd()
    try:
        os.chdir(app_dir)

        result = subprocess.run(
            [
                "hvigorw",
                "assembleApp",
                "-p",
                "buildMode=debug",
                "-p",
                "product=default",
                "--mode",
                "project",
                "--analyze=normal",
                "--parallel",
                "--incremental",
                "--sync",
                "--no-daemon",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=True,
            env=os_env,
            cwd=app_dir,
        )

        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print_error(f"Build failed: {e.stderr}")
        raise
    finally:
        os.chdir(original_dir)


def sign_bundle(env, os_env, sign_tool_path, unsigned_bundle, result_file):
    try:
        result = subprocess.run(
            [
                "java",
                "-jar",
                sign_tool_path,
                "sign-app",
                "-keyAlias",
                "key_alias",
                "-signAlg",
                "SHA256withECDSA",
                "-mode",
                "localSign",
                "-appCertFile",
                "/opt/config/keys/test.cer",
                "-profileFile",
                "/opt/config/keys/testRelease.p7b",
                "-inFile",
                unsigned_bundle,
                "-keystoreFile",
                "/opt/config/keys/store_file.p12",
                "-outFile",
                result_file,
                "-keyPwd",
                "StorePassword",
                "-keystorePwd",
                "StorePassword",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=True,
            env=os_env,
        )

        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print_error(f"Build failed: {e.stderr}")
        raise


def generate_bundle(target, source, env):
    project_path = env.Dir("#").abspath

    app_dir = f"{project_path}/bin/openharmony_template"
    if os.path.exists(app_dir):
        shutil.rmtree(app_dir)

    templ = f"{project_path}/misc/dist/openharmony_template"
    shutil.copytree(templ, app_dir)

    if env["arch"] == "arm64":
        lib_dest_arch = "arm64-v8a"
        stl_lib_arch = "aarch64-linux-ohos"
    elif env["arch"] == "x86_64":
        lib_dest_arch = "x86_64"
        stl_lib_arch = "x86_64-linux-ohos"

    target_dir = f"{app_dir}/entry/libs/{lib_dest_arch}"
    if not os.path.isdir(target_dir):
        os.makedirs(target_dir, exist_ok=True)

    source_path = os.path.join(project_path, str(source[0]))
    shutil.copy(source_path, f"{target_dir}/libgodot.so")

    if env["library_type"] != "static_library":
        stl_lib_path = f"{env['OPENHARMONY_SDK_PATH']}/native/llvm/lib/{stl_lib_arch}/libc++_shared.so"
        stl_lib_dst_path = f"{target_dir}/libc++_shared.so"
        shutil.copy(stl_lib_path, stl_lib_dst_path)

    file_prefix = "godot_" + env["platform"] + "_" + env["target"]
    if env.dev_build:
        file_prefix += "_dev"
    if env["precision"] == "double":
        file_prefix += "_double"

    result_name = (file_prefix + env.extra_suffix + env.module_version_string).replace(".", "_") + "_" + env["arch"]
    result_dir = f"{project_path}/bin/{result_name}"

    if env.editor_build:
        cmd_line_tools_path = env["OPENHARMONY_CMD_LINE_TOOL_PATH"]
        os_env = setup_hvigor_env(cmd_line_tools_path)
        build_bundle(env, os_env, app_dir)
        sign_tool_path = f"{cmd_line_tools_path}/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar"
        unsigned_bundle = os.path.join(
            app_dir, "build", "outputs", "default", "openharmony_template-default-unsigned.app"
        )
        result_file = f"{result_dir}.app"
        sign_bundle(env, os_env, sign_tool_path, unsigned_bundle, result_file)
    else:
        shutil.make_archive(result_dir, "zip", root_dir=app_dir)

    shutil.rmtree(app_dir)
