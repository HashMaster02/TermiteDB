# TermiteDB

TermiteDB is a command-line tool for storing key-value pairs. It's based on Bitcask, a log-structured storage engine that uses an hashtable to point to information in a file.

## Motivation

I was frustrated with having to copy-paste pieces of information from one source to another multiple times. The clipboard is handy, but it has no organizational structure to it. TermiteDB solves that issue by allowing you to store frequently accessed information quickly with a single command-line call. The best part: it is a single binary with no installation required.

# Quick Start

Download the Linux build from the [latest release](https://github.com/HashMaster02/termiteDB/releases/latest), then:

```sh
tar -xzf termite-v1.0.0-linux-x86_64.tar.gz
chmod +x termite
./termite --version
```

Move it onto your `PATH` if you want it available everywhere:

```sh
sudo mv termite /usr/local/bin/
```

TermiteDB keeps its data in a `seg/` directory inside whatever directory you run
it from, and that directory must already exist:

```sh
mkdir seg
termite name:termite
termite name
# termite
```

Because the store is relative to your working directory, each project directory
gets its own independent set of keys.

# Usage

| Command | What it does |
| --- | --- |
| `termite key:value` | Store `value` under `key` |
| `termite "key: value"` | Same, for values containing spaces (the space after the colon is kept) |
| `termite key` | Print the value stored under `key` |
| `termite -d key` | Delete `key` |
| `termite -c` | Compact the segment files, reclaiming space from overwritten and deleted keys |
| `termite --version` | Print the version |

Several operations can be chained in one call:

```sh
termite host:localhost port:5432 -d oldkey user
```

`-c` is the exception — it must be passed on its own, with no other arguments.

## Notes

- Writes are appended to a log segment; once a segment passes its size limit a
  new one is started. Reads go through an in-memory index rebuilt at startup.
- Deletes write a tombstone rather than rewriting the log. Run `termite -c` to
  actually reclaim the space.
- Reading a key that does not exist prints `Missing key '<key>'` and still exits 0.

# Building from source

```sh
make                  # debug build -> termite_d (AddressSanitizer enabled)
make MODE=release     # release build -> termite (optimized, stripped)
make dist             # release build + tarball + sha256 checksum
```

The version number lives in one place: `VERSION` in the makefile.
