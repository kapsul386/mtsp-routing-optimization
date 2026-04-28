from __future__ import annotations

import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def resolve_build_dir(build_dir: str | Path = "build") -> Path:
    build_path = Path(build_dir)
    return build_path if build_path.is_absolute() else ROOT / build_path


def get_executable_path(name: str = "tsp", build_dir: str | Path = "build") -> Path:
    build_path = resolve_build_dir(build_dir)
    candidates = [
        build_path / "src" / "Release" / f"{name}.exe",
        build_path / "src" / "Debug" / f"{name}.exe",
        build_path / "src" / f"{name}.exe",
        build_path / "src" / name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def get_all_source_files() -> list[Path]:
    files: list[Path] = []

    for candidate in [ROOT / "CMakeLists.txt", ROOT / "src" / "CMakeLists.txt"]:
        if candidate.exists():
            files.append(candidate)

    for directory in [ROOT / "src", ROOT / "include"]:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".c", ".h", ".hpp"}:
                files.append(path)

    return files


def need_rebuild(exe_path: str | Path | None = None) -> bool:
    executable = Path(exe_path) if exe_path is not None else get_executable_path()
    if not executable.exists():
        return True

    exe_mtime = executable.stat().st_mtime
    for source_file in get_all_source_files():
        if source_file.stat().st_mtime > exe_mtime:
            return True
    return False


def _run_command(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def _is_stale_cmake_cache(stderr: str) -> bool:
    lowered = stderr.lower()
    return (
        "cmakecache.txt directory" in lowered and "different than the directory" in lowered
    ) or "does not match the source" in lowered


def compile_project(build_dir: str | Path = "build") -> bool:
    print("Compiling project...")
    build_path = resolve_build_dir(build_dir)
    build_path.mkdir(parents=True, exist_ok=True)

    config_cmd = ["cmake", "-S", str(ROOT), "-B", str(build_path), "-DCMAKE_BUILD_TYPE=Release"]
    result = _run_command(config_cmd, ROOT)
    if result.returncode != 0 and _is_stale_cmake_cache(result.stderr):
        safe_remove_build_dir(build_path)
        build_path.mkdir(parents=True, exist_ok=True)
        result = _run_command(config_cmd, ROOT)

    if result.returncode != 0:
        fallback_cmd = config_cmd + ["-G", "MinGW Makefiles"]
        result = _run_command(fallback_cmd, ROOT)
        if result.returncode != 0 and _is_stale_cmake_cache(result.stderr):
            safe_remove_build_dir(build_path)
            build_path.mkdir(parents=True, exist_ok=True)
            result = _run_command(fallback_cmd, ROOT)

    if result.returncode != 0:
        raise RuntimeError(f"CMake failed: {result.stderr}")

    result = _run_command(["cmake", "--build", str(build_path), "--config", "Release"], ROOT)
    if result.returncode != 0:
        raise RuntimeError(f"Build failed: {result.stderr}")

    print("Compiling complete")
    return True


def safe_remove_build_dir(build_dir: str | Path = "build") -> None:
    build_path = resolve_build_dir(build_dir).resolve()
    if not build_path.is_relative_to(ROOT.resolve()):
        raise RuntimeError(f"Refusing to remove build dir outside project root: {build_path}")
    if build_path.exists():
        shutil.rmtree(build_path)


def recompiles_if_necessary(build_dir: str | Path = "build", exe_path: str | Path | None = None) -> None:
    if need_rebuild(exe_path):
        compile_project(build_dir)
