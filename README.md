# btrfs-cli

A small macOS command-line tool that reads a btrfs filesystem image directly. It doesn't mount anything and doesn't need FUSE or a kernel driver. It can:

- **list** every file in the image, recursively, and
- **get** the contents of a file at a given path.

It is built on the read-only btrfs implementation from [btrfs-fuse](https://github.com/adam900710/btrfs-fuse) by Qu Wenruo, compiled without its FUSE front end. The `btrfs-fuse/` checkout is used as-is and is never modified. All macOS porting lives in this repo (`compat/`, `src/`).

## Build

```sh
brew install pkg-config libb2 openssl@3 xxhash zstd lzo
git submodule update --init   # fetches btrfs-fuse
make
```

The binary is `build/btrfs-cli`. `make install` copies it to `/usr/local/bin` (override with `PREFIX=`). If `lzo` is not installed, the build still works, but LZO-compressed extents can't be read.

## Usage

```
btrfs-cli list [-l] [-d <device>]... <image> [<path>]
btrfs-cli get  [-o <output>] [-d <device>]... <image> <path>
```

```sh
# Every path in the image
btrfs-cli list disk.img

# Long listing of /etc: mode, size, mtime, symlink targets
btrfs-cli list -l disk.img /etc

# Extract a file to stdout, or to a file
btrfs-cli get disk.img /etc/hostname
btrfs-cli get -o backup.tar disk.img /home/me/backup.tar
```

Paths are absolute within the filesystem's default subvolume. Other subvolumes show up as directories where they are linked into the tree. For a multi-device filesystem, pass the first image as `<image>` and add each extra device with `-d`.

`ls` and `cat` are accepted as aliases for `list` and `get`.

## What it can't do

These come from btrfs-fuse:

- It is read-only by design.
- It doesn't read extended attributes (xattrs), a gap btrfs-fuse also lists.

## Making a test image

On Linux, or in a Linux container, `mkfs.btrfs` can build an image from a directory without mounting it:

```sh
truncate -s 256M test.img
mkfs.btrfs --rootdir ./some-dir test.img
```

## macOS porting notes

- `compat/include/` provides stand-ins for Linux-only headers (`linux/types.h`, `asm/types.h`, `byteswap.h`, `linux/limits.h`, `uuid.h`). Its `byteswap.h` also defines glibc's `__BYTE_ORDER` macros. Without them, btrfs-fuse's endianness check is wrong on macOS and every on-disk field gets byte-swapped.
- `compat/prelude.h` is force-included into every file. It maps `st_[amc]tim` to macOS's `st_[amc]timespec` and `EUCLEAN` to `EFTYPE`, and routes `pread` through `btrfs_cli_pread()` (`src/compat.c`). Upstream's `btrfs_read_from_disk()` loops forever when `pread` returns 0 at end of file, which happens with any truncated or non-btrfs input. The wrapper turns that into `EIO`.
- `src/messages.c` replaces upstream's `messages.c`. It expands glibc's `%m` and sends every message to stderr, so `get` can stream clean data to stdout.

## Patches to btrfs-fuse

Fixes for upstream bugs live in `patches/` as `-p1` diffs against the btrfs-fuse root. At build time the Makefile copies each affected file (listed in `PATCHED_SRC`) into `build/patched/`, applies every patch there and compiles that copy. The submodule itself stays untouched. When upstream fixes a bug, delete its patch, and remove the file from `PATCHED_SRC` if nothing else patches it.

| Patch | Fixes |
| --- | --- |
| `data-inline-compression.patch` | Small files that btrfs stores compressed inside its metadata failed to read with `invalid compression algorithm: 0`. This affects any filesystem written with `compress=`. |

## Licensing

btrfs-cli is licensed under the **GNU General Public License v2.0** (GPL-2.0-only); see [LICENSE](LICENSE). This matches the most restrictive code it builds in: btrfs-fuse's `libs/raid56.[ch]`, `libs/tables.c` and `libs/list.h` are GPL-2.0-only. The rest of btrfs-fuse is MIT or GPL-2.0-or-later, and both are compatible with GPL-2.0.
