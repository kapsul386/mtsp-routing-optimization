import os
import shutil
import subprocess
from pathlib import Path


def get_executable_path(name="tsp", build_dir="build"):
    candidates = [
        Path(build_dir) / "src" / "Release" / f"{name}.exe",
        Path(build_dir) / "src" / "Debug" / f"{name}.exe",
        Path(build_dir) / "src" / f"{name}.exe",
        Path(build_dir) / "src" / name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]

def get_all_source_files():
    source_extensions = ('.cpp', '.c', '.h', '.hpp')
    cmake_extensions = ('.cmake', 'CMakeLists.txt')
    source_dirs = ('src', 'include')
    all_files = ['CMakeLists.txt']

    for source_dir in source_dirs:
        for root, dirs, files in os.walk(source_dir):
            for file in files:
                if file.endswith(source_extensions):
                    all_files.append(os.path.join(root, file))

    for root, dirs, files in os.walk("."):
        for file in files:
            if file.endswith(cmake_extensions):
                all_files.append(os.path.join(root, file))

    return list(all_files)

def need_rebuild(exe_path=None):
    exe_path = Path(exe_path) if exe_path is not None else get_executable_path()
    if not exe_path.exists():
        return True

    exe_mtime = exe_path.stat().st_mtime

    for source_file in get_all_source_files():
        if os.path.getmtime(source_file) > exe_mtime:
            return True

    return False

def compile_project(build_dir="build"):
    print("Compiling project...")
    os.makedirs(build_dir, exist_ok=True)

    config_cmd = ["cmake", "-S", ".", "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"]

    result = subprocess.run(
        config_cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace"
    )

    if result.returncode != 0:
        if os.path.exists('build'):
            shutil.rmtree('build')
        result = subprocess.run(
            config_cmd + ["-G", "MinGW Makefiles"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace"
        )

    if result.returncode != 0:
        raise RuntimeError(f"CMake failed: {result.stderr}")

    result = subprocess.run(
        ["cmake", "--build", build_dir, "--config", "Release"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace"
    )

    if result.returncode != 0:
        raise RuntimeError(f"Build failed: {result.stderr}")

    print("Compiling complete")
    return True

def recompiles_if_necessary(build_dir="build", exe_path=None):
    if need_rebuild(exe_path):
        compile_project(build_dir)
