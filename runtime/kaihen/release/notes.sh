#!/bin/sh
# SPDX-License-Identifier: ISC
#
# Prints the notes of a kaiHEN release: the annotated tag's message, then the
# zip's layout, the sources that the Dockerfile pins and the zip's SHA-256.
#   sh release/notes.sh VERSION ZIP DEVKIT_VERSION
set -eu

version="${1:?usage: notes.sh VERSION ZIP DEVKIT_VERSION}"
zip="${2:?usage: notes.sh VERSION ZIP DEVKIT_VERSION}"
devkit="${3:?usage: notes.sh VERSION ZIP DEVKIT_VERSION}"
top=$(cd "$(dirname "$0")/.." && pwd)

# The value of an ARG in the Dockerfile.
pin() {
    sed -n "s/^ARG $1=//p" "$top/Dockerfile"
}

if [ "$(git -C "$top" cat-file -t "refs/tags/$version" 2> /dev/null)" = tag ]; then
    git -C "$top" tag -l --format='%(contents)' "$version"
    echo
fi

cat <<NOTES
**Warning:** this is still a work in progress, and might have bugs. kaiHEN allows full access to the system, including the system NAND. A malicious homebrew might write to your Switch 2 and brick it forever.

**Known issue:** files whose names have non-ASCII characters (accents, Japanese, emoji) do not show in folder listings, in OunceShell or over FTP. They can still be opened, downloaded and deleted by name.

kaiHEN-$version.zip layout:

- \`bootstrap.bin\`, \`hbmenu.nro\`
- \`ro/main\`, \`hen/main\`
- \`game/main\`: nx-hbloader
- \`programs/ounceshell.nro\`: OunceShell
- \`licenses/\`: the licenses of everything above

Built with \`docker build --build-arg RELEASE=1 --output out .\` at this tag, with ounce-devkit $devkit.

\`libkaihen-$version-1-any.pkg.tar.zst\`: libkaihen (\`kaihen.h\`, \`-lkaihen\`, \`-lkaihen_plugin\`) for devkitPro's pacman, built with \`packaging/Dockerfile\`. It is also in the [ounce] repository at https://pkg.ouncebrew.org.

## Source

- kaiHEN: this repository at this tag.
- nx-hbloader: [ouncebrew/ounce-hbloader](https://github.com/ouncebrew/ounce-hbloader) at \`$(pin HBLOADER_COMMIT)\`.
- nx-hbmenu: [ouncebrew/ounce-hbmenu](https://github.com/ouncebrew/ounce-hbmenu) at \`$(pin HBMENU_COMMIT)\`.
- OunceShell (GPL-3.0-or-later): [kurumuz/ounceshell](https://github.com/kurumuz/ounceshell) at \`$(pin OUNCESHELL_COMMIT)\`, built with its \`switch2/Makefile\`. Its FTP server is [mtheall/ftpd](https://github.com/mtheall/ftpd) v2.3.1 (public domain), modified.
- libounce, OunceVK and libkaihen: [ouncebrew/ounce-devkit](https://github.com/ouncebrew/ounce-devkit) $devkit, which pins their commits.
- The devkitPro portlibs linked into hbmenu and OunceShell, from devkitPro's packages: libconfig 1.7.2, freetype 2.13.3, harfbuzz 10.0.1, libjpeg-turbo 2.1.2, libpng 1.6.48, bzip2 1.0.8, zlib 1.3.1, physfs 3.2.0, SDL2 2.28.5, SDL2_ttf 2.22.0, SDL2_image 2.0.4, libwebp 1.0.2, libarchive 3.7.2, expat 2.2.5, lz4 1.9.3, zstd 1.5.7, liblzma 5.8.2, libvorbis 1.3.7, libogg 1.3.4, Oniguruma 6.9.2.

SHA-256: $(sha256sum "$zip" | cut -d' ' -f1)
NOTES
