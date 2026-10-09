#!/bin/bash
# Build a relocatable, ad-hoc-signed Link Control application bundle.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DAEMON_SOURCE="$ROOT/../linkctl-daemon"
RAYCAST_SOURCE="$ROOT/../../integrations/raycast"
NOTICE_SOURCE="$ROOT/../../NOTICE.md"
OUTPUT=${OUTPUT:-"$ROOT/dist/Link Control.app"}
ZIP_OUTPUT=${ZIP_OUTPUT:-"${OUTPUT%.app}.zip"}
CHECKSUM_OUTPUT=${CHECKSUM_OUTPUT:-"${ZIP_OUTPUT}.sha256"}
METADATA_OUTPUT=${METADATA_OUTPUT:-"${ZIP_OUTPUT%.zip}.metadata.json"}
BUILD_DIR=${BUILD_DIR:-"$ROOT/.build/linkctl-daemon-release"}

for command in cmake swift otool vtool lipo install_name_tool codesign ditto python3 plutil shasum; do
    command -v "$command" >/dev/null || { echo "package-app.sh: required command not found: $command" >&2; exit 1; }
done
[ -f "$DAEMON_SOURCE/CMakeLists.txt" ] || { echo "package-app.sh: daemon source not found: $DAEMON_SOURCE" >&2; exit 1; }
[ -f "$RAYCAST_SOURCE/lib/link-control.sh" ] || { echo "package-app.sh: Raycast scripts not found: $RAYCAST_SOURCE" >&2; exit 1; }
[ -f "$NOTICE_SOURCE" ] || { echo "package-app.sh: project NOTICE.md not found: $NOTICE_SOURCE" >&2; exit 1; }

SOURCE_VERSION=$(plutil -extract CFBundleShortVersionString raw "$ROOT/Info.plist")
SOURCE_BUILD=$(plutil -extract CFBundleVersion raw "$ROOT/Info.plist")
VERSION=${VERSION:-"$SOURCE_VERSION"}
BUILD_NUMBER=${BUILD_NUMBER:-"$SOURCE_BUILD"}
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "package-app.sh: VERSION must be numeric x.y.z (got $VERSION)" >&2; exit 1;
}
[[ "$BUILD_NUMBER" =~ ^[1-9][0-9]{0,17}$ ]] || {
    echo "package-app.sh: BUILD_NUMBER must be a positive integer of at most 18 digits (got $BUILD_NUMBER)" >&2; exit 1;
}

mkdir -p "$(dirname "$OUTPUT")" "$(dirname "$ZIP_OUTPUT")" "$(dirname "$CHECKSUM_OUTPUT")" "$(dirname "$METADATA_OUTPUT")"
STAGE=$(mktemp -d "$(dirname "$OUTPUT")/.Link-Control-package.XXXXXX")
APP="$STAGE/Link Control.app"
RELOCATE=""
cleanup() {
    rm -rf "$STAGE"
    if [ -n "$RELOCATE" ]; then rm -rf "$RELOCATE"; fi
}
trap cleanup EXIT

echo "Building release GUI..."
cd "$ROOT"
swift build -c release --product LinkControlGUI
GUI_BIN=$(swift build -c release --show-bin-path)/LinkControlGUI

echo "Building release daemon..."
cmake -S "$DAEMON_SOURCE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --config Release --target linkctl-daemon
DAEMON_BIN="$BUILD_DIR/linkctl-daemon"
[ -x "$DAEMON_BIN" ] || { echo "package-app.sh: daemon build produced no executable" >&2; exit 1; }

plutil -lint "$ROOT/Info.plist"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Helpers" "$APP/Contents/Frameworks" "$APP/Contents/Resources/ThirdPartyNotices"
cp "$GUI_BIN" "$APP/Contents/MacOS/LinkControlGUI"
cp "$DAEMON_BIN" "$APP/Contents/Helpers/linkctl-daemon"
cp "$ROOT/Info.plist" "$APP/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$VERSION" "$APP/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$BUILD_NUMBER" "$APP/Contents/Info.plist"
cp "$NOTICE_SOURCE" "$APP/Contents/Resources/NOTICE.md"
cp -R "$RAYCAST_SOURCE" "$APP/Contents/Resources/Raycast"
chmod +x "$APP/Contents/Resources/Raycast/"*.sh

# Resolve and copy every non-Apple dependency of the daemon.  The manifest is
# also the source of truth for notices, avoiding a stale hand-maintained list.
python3 - "$APP" "$DAEMON_BIN" <<'PY'
import json, os, re, shutil, subprocess, sys

app, daemon = map(os.path.realpath, sys.argv[1:])
frameworks = os.path.join(app, "Contents", "Frameworks")
system_prefixes = ("/usr/lib/", "/System/Library/")

def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)

def deps(path):
    lines = run("otool", "-L", path).splitlines()[1:]
    return [line.strip().split(" (compatibility version", 1)[0] for line in lines if line.startswith("\t")]

def rpaths(path):
    # otool -l output is deliberately parsed narrowly: an LC_RPATH has one path line.
    lines = run("otool", "-l", path).splitlines()
    answer = []
    for i, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            for candidate in lines[i + 1:i + 6]:
                match = re.match(r"\s*path (.+) \(offset", candidate)
                if match:
                    answer.append(match.group(1)); break
    return answer

def resolve(ref, owner):
    if ref.startswith(system_prefixes): return None
    candidates = []
    if ref.startswith("@loader_path/"):
        candidates.append(os.path.join(os.path.dirname(owner), ref[len("@loader_path/"):]))
    elif ref.startswith("@executable_path/"):
        candidates.append(os.path.join(os.path.dirname(daemon), ref[len("@executable_path/"):]))
    elif ref.startswith("@rpath/"):
        tail = ref[len("@rpath/"):]
        for rp in rpaths(owner):
            rp = rp.replace("@loader_path", os.path.dirname(owner)).replace("@executable_path", os.path.dirname(daemon))
            candidates.append(os.path.join(rp, tail))
    elif ref.startswith("/"):
        candidates.append(ref)
    else:
        raise RuntimeError(f"unsupported install name {ref!r} in {owner}")
    for candidate in candidates:
        candidate = os.path.realpath(candidate)
        if os.path.isfile(candidate): return candidate
    raise RuntimeError(f"unresolved non-system dependency {ref!r} required by {owner}")

queue, copied, install_names = [daemon], {}, {}
while queue:
    owner = queue.pop(0)
    for ref in deps(owner):
        source = resolve(ref, owner)
        if source is None: continue
        base = os.path.basename(source)
        # A flat Frameworks directory is safe only when names are unique.
        previous = copied.get(base)
        if previous and previous != source:
            raise RuntimeError(f"dependency filename collision: {previous} and {source}")
        if not previous:
            copied[base] = source
            shutil.copy2(source, os.path.join(frameworks, base))
            queue.append(source)
        # Homebrew's install-name symlink can differ from the versioned file
        # name (for example libcjson.1.dylib -> libcjson.1.7.19.dylib). Keep a
        # bundle-local alias so the rewritten reference still resolves.
        ref_base = os.path.basename(ref)
        if ref_base != base:
            alias = os.path.join(frameworks, ref_base)
            if os.path.lexists(alias) and os.path.realpath(alias) != os.path.join(frameworks, base):
                raise RuntimeError(f"dependency filename collision for alias {ref_base}")
            if not os.path.lexists(alias): os.symlink(base, alias)
        install_names[(owner, ref)] = base

formulas = set()
for source in copied.values():
    match = re.search(r"/Cellar/([^/]+)/", source) or re.search(r"/(?:opt/homebrew|usr/local)/opt/([^/]+)/", source)
    if match: formulas.add(match.group(1))
json.dump({"libraries": copied, "formulae": sorted(formulas)}, open(os.path.join(app, "Contents", "Resources", "bundle-manifest.json"), "w"), indent=2, sort_keys=True)
PY

# Rewrite every bundled image to use only paths within the bundle.  Deleting
# rpaths is intentional: all dependencies below are explicit @loader_path paths.
for image in "$APP/Contents/Helpers/linkctl-daemon" "$APP"/Contents/Frameworks/*; do
    [ -f "$image" ] || continue
    [ -L "$image" ] && continue
    if [[ "$image" == "$APP/Contents/Helpers/"* ]]; then relative='@loader_path/../Frameworks'; else relative='@loader_path'; fi
    while IFS= read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/Library/*) ;;
            *) base=$(basename "$dep"); [ -f "$APP/Contents/Frameworks/$base" ] || { echo "package-app.sh: unresolved bundled dependency $dep in $image" >&2; exit 1; }; install_name_tool -change "$dep" "$relative/$base" "$image" ;;
        esac
    done < <(otool -L "$image" | tail -n +2 | sed -E 's/^[[:space:]]*([^[:space:]]+).*/\1/')
    if [[ "$image" == "$APP/Contents/Frameworks/"* ]]; then install_name_tool -id "@loader_path/$(basename "$image")" "$image"; fi
    while IFS= read -r rpath; do install_name_tool -delete_rpath "$rpath" "$image"; done < <(otool -l "$image" | awk '/cmd LC_RPATH/{find=1; next} find && /path /{print $2; find=0}')
done

# Preserve licenses for the formulae actually copied above. Formula prefixes
# are queried rather than assumed, so Intel and Apple Silicon Homebrew work.
MANIFEST="$APP/Contents/Resources/bundle-manifest.json"
python3 - "$MANIFEST" <<'PY' | while IFS= read -r formula; do
import json, sys
for formula in json.load(open(sys.argv[1]))['formulae']: print(formula)
PY
    prefix=$(brew --prefix "$formula" 2>/dev/null || true)
    [ -n "$prefix" ] || { echo "package-app.sh: cannot locate Homebrew formula for bundled library: $formula" >&2; exit 1; }
    found=0
    while IFS= read -r notice; do cp "$notice" "$APP/Contents/Resources/ThirdPartyNotices/${formula}-$(basename "$notice")"; found=1; done < <(find -L "$prefix" -maxdepth 2 -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'NOTICE*' \) -print)
    [ "$found" = 1 ] || echo "No license file found in Homebrew prefix for $formula; see Homebrew formula metadata." > "$APP/Contents/Resources/ThirdPartyNotices/${formula}-NOTICE.txt"
done

# Reject any host Homebrew/build references after rewriting.
if otool -L "$APP/Contents/Helpers/linkctl-daemon" "$APP"/Contents/Frameworks/* | grep -E '(/opt/homebrew|/usr/local|@rpath)'; then
    echo "package-app.sh: package retains a host dependency reference" >&2; exit 1
fi
if otool -l "$APP/Contents/Helpers/linkctl-daemon" "$APP"/Contents/Frameworks/* | grep -A2 'cmd LC_RPATH' | grep -E '(/opt/homebrew|/usr/local|/build/)'; then
    echo "package-app.sh: package retains an absolute build rpath" >&2; exit 1
fi

# The source plist describes the project's intended deployment target, but a
# packaged app cannot honestly claim a lower version than any embedded Mach-O.
# Derive its value from the actual GUI, helper, and copied libraries instead.
BUILD_METADATA=$(python3 - "$APP" <<'PY'
import os, re, subprocess, sys

app = sys.argv[1]
images = [os.path.join(app, "Contents", "MacOS", "LinkControlGUI"),
          os.path.join(app, "Contents", "Helpers", "linkctl-daemon")]
frameworks = os.path.join(app, "Contents", "Frameworks")
images.extend(os.path.join(frameworks, name) for name in os.listdir(frameworks)
              if os.path.isfile(os.path.join(frameworks, name)) and not os.path.islink(os.path.join(frameworks, name)))

def version(value):
    return tuple(int(x) for x in value.split('.'))

minimums, architectures = [], None
for image in images:
    try:
        output = subprocess.check_output(["vtool", "-show-build", image], text=True, stderr=subprocess.STDOUT)
        archs = subprocess.check_output(["lipo", "-archs", image], text=True, stderr=subprocess.STDOUT).split()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"cannot inspect Mach-O image {image}: {error.output}")
    found = re.findall(r"\bminos\s+([0-9]+(?:\.[0-9]+)*)", output)
    if not found:
        found = re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([0-9]+(?:\.[0-9]+)*)", output)
    if not found:
        raise SystemExit(f"no macOS minimum version reported by vtool for {image}")
    minimums.extend(found)
    if architectures is None:
        architectures = set(archs)
    elif not architectures.issubset(set(archs)):
        raise SystemExit(f"{image} lacks required GUI architectures: {sorted(architectures - set(archs))}")

maximum = max(minimums, key=version)
print(f"MIN_OS={maximum}")
print(f"ARCHITECTURES={' '.join(sorted(architectures))}")
PY
)
eval "$BUILD_METADATA"
plutil -replace LSMinimumSystemVersion -string "$MIN_OS" "$APP/Contents/Info.plist"
ACTUAL_VERSION=$(plutil -extract CFBundleShortVersionString raw "$APP/Contents/Info.plist")
ACTUAL_BUILD=$(plutil -extract CFBundleVersion raw "$APP/Contents/Info.plist")
[ "$ACTUAL_VERSION" = "$VERSION" ] && [ "$ACTUAL_BUILD" = "$BUILD_NUMBER" ] || {
    echo "package-app.sh: bundle version metadata did not match requested values" >&2; exit 1;
}
echo "Packaged architectures: $ARCHITECTURES"
echo "Packaged minimum macOS: $MIN_OS (maximum reported by bundled Mach-O images)"

# Sign inner code first.  This is development signing only; notarization needs
# a Developer ID certificate and an Apple notary submission outside this script.
find "$APP/Contents/Frameworks" -type f -print0 | while IFS= read -r -d '' image; do codesign --force --sign - --timestamp=none "$image"; done
codesign --force --sign - --timestamp=none "$APP/Contents/Helpers/linkctl-daemon"
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

# --help exits before server/camera initialization; run it from a sparse
# environment and again after moving the bundle through a path with spaces.
# The current C argument parser returns 1 after printing usage, so accept its
# documented usage output rather than treating that historical exit code as a
# packaging failure.
verify_helper_help() {
    local output status
    set +e
    output=$(env -i PATH=/usr/bin:/bin "$1" --help 2>&1)
    status=$?
    set -e
    [[ $status -eq 0 || $status -eq 1 ]] && grep -q 'Usage: ' <<<"$output" || {
        echo "package-app.sh: packaged helper did not provide usable --help" >&2; return 1;
    }
}
verify_helper_help "$APP/Contents/Helpers/linkctl-daemon"
RELOCATE=$(mktemp -d "${TMPDIR:-/tmp}/Link Control relocation.XXXXXX")
cp -R "$APP" "$RELOCATE/Link Control.app"
verify_helper_help "$RELOCATE/Link Control.app/Contents/Helpers/linkctl-daemon"
rm -rf "$RELOCATE"

# Replace only after all validation succeeds, then create the shareable zip.
rm -rf "$OUTPUT"
mv "$APP" "$OUTPUT"
rm -f "$ZIP_OUTPUT"
ditto -c -k --sequesterRsrc --keepParent "$OUTPUT" "$ZIP_OUTPUT"
shasum -a 256 "$ZIP_OUTPUT" > "$CHECKSUM_OUTPUT"
python3 - "$METADATA_OUTPUT" "$ACTUAL_VERSION" "$ARCHITECTURES" "$MIN_OS" <<'PY'
import json, sys

path, version, architectures, minimum_os = sys.argv[1:]
with open(path, "w") as output:
    json.dump({"version": version, "arch": architectures.split(), "minOS": minimum_os,
               "signing": "ad-hoc"}, output, separators=(",", ":"))
    output.write("\n")
PY
echo "Created: $OUTPUT"
echo "Created: $ZIP_OUTPUT"
echo "Created: $CHECKSUM_OUTPUT"
echo "Created: $METADATA_OUTPUT"
