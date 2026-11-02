#!/usr/bin/env bash
set -euo pipefail
: "${RELEASE_TAG:?RELEASE_TAG must be provided}"
: "${GH_REPO:?GH_REPO must be provided}"
: "${GH_TOKEN:?GH_TOKEN must be provided}"
if [[ ! "$RELEASE_TAG" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]]; then
    echo 'Invalid release tag' >&2
    exit 1
fi
(
    cd dist
    sha256sum --strict --check SHA256SUMS.txt
)
assets=(dist/ImageCompare.exe "dist/ImageCompare-$RELEASE_TAG-windows-x64.zip"
        "dist/ImageCompare-$RELEASE_TAG-source.zip" dist/BUILD-INFO.txt dist/SHA256SUMS.txt)
for file in "${assets[@]}"; do test -s "$file"; done

if draft=$(gh release view "$RELEASE_TAG" --json isDraft --jq '.isDraft' 2>/dev/null); then
    if [[ "$draft" == 'false' ]]; then
        echo "Release $RELEASE_TAG is already published; preserving its assets."
        exit 0
    fi
else
    # Keep the release a draft until every tested asset has uploaded successfully.
    gh release create "$RELEASE_TAG" --verify-tag --draft \
        --title "ImageCompare $RELEASE_TAG" --generate-notes
fi

gh release upload "$RELEASE_TAG" "${assets[@]}" --clobber
if [[ "$RELEASE_TAG" == *-* ]]; then
    gh release edit "$RELEASE_TAG" --draft=false --prerelease --latest=false
else
    gh release edit "$RELEASE_TAG" --draft=false --prerelease=false --latest
fi
