"""Download and build third-party dependencies for the PC port."""
import io
import os
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# RECVX_DEPS overrides the location (the share package keeps its tools outside the repository).
DEPS = os.environ.get("RECVX_DEPS") or os.path.join(ROOT, "build_pc", "deps")

SDL3_VERSION = "3.4.18"
SDL3_SOURCE_URL = (
    f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL3_VERSION}/"
    f"SDL3-{SDL3_VERSION}.zip"
)
SDL3_SOURCE_ROOT = os.path.join(DEPS, f"SDL3-source-{SDL3_VERSION}")
SDL3_BUILD_ROOT = os.path.join(DEPS, f"SDL3-build-{SDL3_VERSION}-x86")
SDL3_PACKAGE = os.path.join(DEPS, f"SDL3-{SDL3_VERSION}")
SDL3_STAMP = os.path.join(SDL3_PACKAGE, ".recvx-hidapi-libusb-enabled")

VCPKG_TRIPLET = "x86-windows"

# FFmpeg is only used by pc/tools/convert_movies.py (converting the disc's movies once at setup);
# it is not linked into or shipped with the game.
FFMPEG_URL = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"


def ffmpeg_exe():
    """Path of ffmpeg.exe, downloaded on first use."""
    for dirpath, _, files in os.walk(DEPS):
        if "ffmpeg.exe" in files:
            return os.path.join(dirpath, "ffmpeg.exe")
    os.makedirs(DEPS, exist_ok=True)
    print(f"downloading {FFMPEG_URL}")
    data = urllib.request.urlopen(FFMPEG_URL).read()
    zipfile.ZipFile(io.BytesIO(data)).extractall(os.path.join(DEPS, "ffmpeg"))
    return ffmpeg_exe()


def sdl3_dir():
    """Directory exposing the SDL3 include and x86 import-library layout expected by build.py."""
    return SDL3_PACKAGE


def _run(command, cwd=None, env=None):
    print("+ " + subprocess.list2cmdline([str(arg) for arg in command]))
    result = subprocess.run(command, cwd=cwd, env=env)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command[0]}")
    return result


def _find_cmake():
    candidates = [shutil.which("cmake"), r"C:\Program Files\CMake\bin\cmake.exe"]
    vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if os.path.isfile(vswhere):
        result = subprocess.run(
            [vswhere, "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, errors="replace",
        )
        install = result.stdout.strip()
        if install:
            candidates.append(os.path.join(
                install, r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            ))
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return os.path.abspath(candidate)
    raise RuntimeError(
        "CMake was not found. Install the CMake component in Visual Studio 2022 "
        "or install CMake for Windows."
    )


def _source_dir():
    """Download SDL's official source archive and return its root directory."""
    os.makedirs(SDL3_SOURCE_ROOT, exist_ok=True)
    for dirpath, dirnames, filenames in os.walk(SDL3_SOURCE_ROOT):
        if ("CMakeLists.txt" in filenames and
                os.path.isdir(os.path.join(dirpath, "include", "SDL3")) and
                os.path.isdir(os.path.join(dirpath, "src"))):
            return dirpath

    shutil.rmtree(SDL3_SOURCE_ROOT, ignore_errors=True)
    os.makedirs(SDL3_SOURCE_ROOT, exist_ok=True)
    print(f"downloading SDL {SDL3_VERSION} source: {SDL3_SOURCE_URL}")
    data = urllib.request.urlopen(SDL3_SOURCE_URL).read()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        archive.extractall(SDL3_SOURCE_ROOT)

    for dirpath, dirnames, filenames in os.walk(SDL3_SOURCE_ROOT):
        if ("CMakeLists.txt" in filenames and
                os.path.isdir(os.path.join(dirpath, "include", "SDL3")) and
                os.path.isdir(os.path.join(dirpath, "src"))):
            return dirpath
    raise RuntimeError(f"Could not find SDL source tree in {SDL3_SOURCE_ROOT}")


def _find_vcpkg():
    """Find vcpkg, or bootstrap a private copy in the dependency cache."""
    candidates = [
        os.environ.get("VCPKG_ROOT"),
        os.environ.get("VCPKG_INSTALLATION_ROOT"),
        r"C:\vcpkg",
        os.path.join(DEPS, "vcpkg"),
    ]
    for root in candidates:
        if root and os.path.isfile(os.path.join(root, "vcpkg.exe")):
            return os.path.abspath(root)

    root = os.path.join(DEPS, "vcpkg")
    git = shutil.which("git")
    if not git:
        raise RuntimeError("Git is required to bootstrap vcpkg for SDL's libusb dependency.")
    if not os.path.isdir(os.path.join(root, ".git")):
        if os.path.exists(root):
            shutil.rmtree(root)
        _run([git, "clone", "--depth", "1", "https://github.com/microsoft/vcpkg.git", root])

    bootstrap = os.path.join(root, "bootstrap-vcpkg.bat")
    if not os.path.isfile(os.path.join(root, "vcpkg.exe")):
        print("bootstrapping vcpkg for the 32-bit Windows libusb dependency")
        result = subprocess.run(
            f'"{bootstrap}" -disableMetrics', cwd=root, shell=True,
        )
        if result.returncode:
            raise RuntimeError("vcpkg bootstrap failed.")
    if not os.path.isfile(os.path.join(root, "vcpkg.exe")):
        raise RuntimeError("vcpkg bootstrap finished without producing vcpkg.exe.")
    return os.path.abspath(root)


def _install_libusb(vcpkg_root):
    """Install an x86 DLL/import library pair so SDL can use the adapter's USB interface."""
    vcpkg = os.path.join(vcpkg_root, "vcpkg.exe")
    env = os.environ.copy()
    env["VCPKG_DISABLE_METRICS"] = "1"
    _run([vcpkg, "install", f"libusb:{VCPKG_TRIPLET}", "--disable-metrics"],
         cwd=vcpkg_root, env=env)

    installed = os.path.join(vcpkg_root, "installed", VCPKG_TRIPLET)
    dll = os.path.join(installed, "bin", "libusb-1.0.dll")
    library = os.path.join(installed, "lib", "libusb-1.0.lib")
    if not os.path.isfile(dll):
        raise RuntimeError(f"vcpkg did not produce the required runtime DLL: {dll}")
    if not os.path.isfile(library):
        raise RuntimeError(f"vcpkg did not produce the required import library: {library}")
    return installed, dll


def _run_cmake(cmake, args):
    # msvc.run adds the Visual Studio x86 environment even when build.py was started
    # from an ordinary command prompt. The workflow also repairs PATH casing on Windows.
    import msvc
    result = msvc.run([cmake] + list(args), cwd=ROOT)
    if result.returncode:
        raise RuntimeError(f"CMake command failed ({result.returncode}): {args[0]}")


def _find_output(build_root, name):
    preferred = os.path.join(build_root, "Release", name)
    if os.path.isfile(preferred):
        return preferred
    matches = []
    for dirpath, _, files in os.walk(build_root):
        if name in files:
            matches.append(os.path.join(dirpath, name))
    if matches:
        matches.sort(key=lambda p: (0 if "\\release\\" in p.lower() else 1, len(p)))
        return matches[0]
    raise RuntimeError(f"SDL build did not produce {name} under {build_root}")


def _build_sdl3():
    """Build SDL3 from source with HIDAPI + libusb enabled for the Nintendo Wii U adapter."""
    required = [
        os.path.join(SDL3_PACKAGE, "include", "SDL3", "SDL.h"),
        os.path.join(SDL3_PACKAGE, "lib", "x86", "SDL3.lib"),
        os.path.join(SDL3_PACKAGE, "lib", "x86", "SDL3.dll"),
        os.path.join(SDL3_PACKAGE, "lib", "x86", "libusb-1.0.dll"),
        SDL3_STAMP,
    ]
    if all(os.path.isfile(path) for path in required):
        return

    source = _source_dir()
    vcpkg_root = _find_vcpkg()
    installed, libusb_dll = _install_libusb(vcpkg_root)
    cmake = _find_cmake()
    toolchain = os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")
    if not os.path.isfile(toolchain):
        raise RuntimeError(f"vcpkg CMake toolchain not found: {toolchain}")

    # Link the libusb import library into SDL3.dll and ship the matching DLL beside recvx.exe.
    # This avoids relying on SDL finding a separately loaded libusb DLL at runtime.
    configure = [
        "-S", source, "-B", SDL3_BUILD_ROOT,
        "-G", "Visual Studio 17 2022", "-A", "Win32",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DCMAKE_PREFIX_PATH={installed}",
        f"-DVCPKG_TARGET_TRIPLET={VCPKG_TRIPLET}",
        "-DSDL_SHARED=ON",
        "-DSDL_STATIC=OFF",
        "-DSDL_JOYSTICK=ON",
        "-DSDL_HIDAPI=ON",
        "-DSDL_HIDAPI_JOYSTICK=ON",
        "-DSDL_HIDAPI_LIBUSB=ON",
        "-DSDL_HIDAPI_LIBUSB_SHARED=OFF",
        "-DSDL_TEST_LIBRARY=OFF",
        "-DSDL_TESTS=OFF",
        "-DSDL_EXAMPLES=OFF",
        "-DSDL_INSTALL=OFF",
    ]
    _run_cmake(cmake, ["--version"])
    _run_cmake(cmake, configure)

    cache_file = os.path.join(SDL3_BUILD_ROOT, "CMakeCache.txt")
    if not os.path.isfile(cache_file):
        raise RuntimeError("SDL CMake configure did not produce CMakeCache.txt.")
    with open(cache_file, encoding="utf-8", errors="replace") as f:
        cache = f.read()

    for option in (
        "SDL_JOYSTICK:BOOL=ON",
        "SDL_HIDAPI:BOOL=ON",
        "SDL_HIDAPI_JOYSTICK:BOOL=ON",
        "SDL_HIDAPI_LIBUSB:BOOL=ON",
    ):
        if option not in cache:
            raise RuntimeError(f"SDL CMake configuration did not enable {option}.")

    for variable in ("LibUSB_INCLUDE_PATH", "LibUSB_LIBRARY"):
        entry = next((line for line in cache.splitlines()
                      if line.startswith(variable + ":")), "")
        value = entry.split("=", 1)[1] if "=" in entry else ""
        if not value or "NOTFOUND" in value.upper() or not os.path.exists(value):
            raise RuntimeError(
                f"SDL CMake configuration did not locate {variable}; "
                "refusing to build SDL without libusb."
            )

    _run_cmake(cmake, [
        "--build", SDL3_BUILD_ROOT, "--config", "Release",
        "--target", "SDL3-shared", "--parallel", str(max(2, os.cpu_count() or 2)),
    ])

    dll = _find_output(SDL3_BUILD_ROOT, "SDL3.dll")
    lib = _find_output(SDL3_BUILD_ROOT, "SDL3.lib")
    if not os.path.isfile(libusb_dll):
        raise RuntimeError(f"libusb runtime disappeared during SDL build: {libusb_dll}")

    shutil.rmtree(SDL3_PACKAGE, ignore_errors=True)
    os.makedirs(os.path.join(SDL3_PACKAGE, "lib", "x86"), exist_ok=True)
    shutil.copytree(
        os.path.join(source, "include", "SDL3"),
        os.path.join(SDL3_PACKAGE, "include", "SDL3"),
    )
    out_lib = os.path.join(SDL3_PACKAGE, "lib", "x86")
    shutil.copyfile(dll, os.path.join(out_lib, "SDL3.dll"))
    shutil.copyfile(lib, os.path.join(out_lib, "SDL3.lib"))
    shutil.copyfile(libusb_dll, os.path.join(out_lib, "libusb-1.0.dll"))

    with open(SDL3_STAMP, "w", encoding="utf-8") as f:
        f.write(
            f"SDL {SDL3_VERSION}\n"
            "SDL_JOYSTICK=ON\n"
            "SDL_HIDAPI=ON\n"
            "SDL_HIDAPI_JOYSTICK=ON\n"
            "SDL_HIDAPI_LIBUSB=ON\n"
            "triplet=x86-windows\n"
        )
    print("Built custom SDL3 with HIDAPI/libusb enabled for the Wii U GameCube adapter.")


def fetch():
    os.makedirs(DEPS, exist_ok=True)
    _build_sdl3()


if __name__ == "__main__":
    fetch()
    print(sdl3_dir())
