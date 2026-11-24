#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Prints an OSV-Scanner custom lockfile (the JSON shape of `osv-scanner --format json`) naming
# every C and C++ library dish.exe links by the upstream commit its pin resolves to, so
# OSV-Scanner matches the advisories that carry git ranges. OSV-Scanner reads no vcpkg manifest,
# so without this the scan finds nothing:
#
#   bash scripts/osv-scan.sh --lockfile-script scripts/osv-lockfile.sh .
#
# The libraries and where their versions come from:
#   every port vcpkg.json declares   the version its builtin-baseline resolves, read from
#                                    microsoft/vcpkg's ports/<port>/vcpkg.json at that commit
#   ENet                             the commit CMakeLists.txt fetches
#   Qt                               the version scripts/tools.ps1 pins
#
# A port with no upstream below, or a version whose tag its upstream does not have, fails the
# script, so no library drops out of the scan unnoticed. Needs git, curl and python3.
set -euo pipefail

cd "$(dirname "$0")/.."

fail() {
    echo "osv-lockfile: $*" >&2
    exit 1
}

# The upstream repository of a vcpkg port, and its release tag for a version.
upstream_of() {
    case "$1" in
        libsodium) echo "https://github.com/jedisct1/libsodium" ;;
        openssl) echo "https://github.com/openssl/openssl" ;;
        opus) echo "https://github.com/xiph/opus" ;;
        sdl2) echo "https://github.com/libsdl-org/SDL" ;;
        sentry-native) echo "https://github.com/getsentry/sentry-native" ;;
        *) return 1 ;;
    esac
}
tag_of() {
    case "$1" in
        libsodium) echo "$2-RELEASE" ;;
        openssl) echo "openssl-$2" ;;
        opus) echo "v$2" ;;
        sdl2) echo "release-$2" ;;
        sentry-native) echo "$2" ;;
        *) return 1 ;;
    esac
}

# A tag's commit is the peeled ^{} line when the tag is annotated, else the tag's own line.
tag_commit() {
    local url="$1" tag="$2" refs peeled direct
    refs=$(git ls-remote --tags "$url" "refs/tags/${tag}" "refs/tags/${tag}^{}")
    peeled=$(awk -v ref="refs/tags/${tag}^{}" '$2 == ref { print $1 }' <<< "$refs")
    direct=$(awk -v ref="refs/tags/${tag}" '$2 == ref { print $1 }' <<< "$refs")
    echo "${peeled:-$direct}"
}

baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
[[ "$baseline" =~ ^[0-9a-f]{40}$ ]] || fail "vcpkg.json builtin-baseline '$baseline' is not a commit"
ports=()
while IFS= read -r port; do ports+=("$port"); done < <(python3 -c '
import json
for d in json.load(open("vcpkg.json"))["dependencies"]:
    print(d if isinstance(d, str) else d["name"])
')
[ "${#ports[@]}" -gt 0 ] || fail "vcpkg.json declares no dependencies"

entries=()
add() {
    entries+=("{\"package\":{\"name\":\"$1\",\"commit\":\"$2\"}}")
}

for port in "${ports[@]}"; do
    url=$(upstream_of "$port") || fail "no upstream for vcpkg port '$port'; add it to this script"
    recipe=$(curl -fsSL --retry 3 "https://raw.githubusercontent.com/microsoft/vcpkg/${baseline}/ports/${port}/vcpkg.json") ||
        fail "could not read ports/${port}/vcpkg.json at ${baseline}"
    version=$(python3 -c '
import json, sys
r = json.loads(sys.stdin.read())
print(next(r[k] for k in ("version", "version-semver", "version-date", "version-string") if k in r))
' <<< "$recipe")
    tag=$(tag_of "$port" "$version")
    commit=$(tag_commit "$url" "$tag")
    [[ "$commit" =~ ^[0-9a-f]{40}$ ]] || fail "$url has no tag $tag (vcpkg $port $version)"
    add "$url" "$commit"
done

enet=$(awk '/GIT_REPOSITORY https:\/\/github.com\/cgutman\/enet.git/ { found = 1; next }
            found && $1 == "GIT_TAG" { print $2; exit }' CMakeLists.txt)
[[ "$enet" =~ ^[0-9a-f]{40}$ ]] || fail "no ENet GIT_TAG commit in CMakeLists.txt"
add "https://github.com/cgutman/enet" "$enet"

qt=$(sed -nE "s/^\\\$script:DishQtVersion = '([0-9.]+)'.*/\\1/p" scripts/tools.ps1)
[[ "$qt" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "no Qt version in scripts/tools.ps1"
qtbase=$(tag_commit "https://github.com/qt/qtbase" "v${qt}")
[[ "$qtbase" =~ ^[0-9a-f]{40}$ ]] || fail "https://github.com/qt/qtbase has no tag v${qt}"
add "https://github.com/qt/qtbase" "$qtbase"

joined=$(IFS=,; echo "${entries[*]}")
printf '{"results":[{"packages":[%s]}]}\n' "$joined"
