#!/usr/bin/env python3
"""Copy files out of a Windows Installer package, with nothing but Python.

    msi_extract.py PACKAGE.msi OUT_DIR KEY=NAME [KEY=NAME ...]

Writes the file the package's cabinet stores under KEY to OUT_DIR/NAME,
for each pair, and fails if any KEY is not there. Used by
cmake/LSWGuestShares.cmake to take the virtio-fs driver and virtiofs.exe
out of virtio-win's guest-tools package, which carries them under keys
such as FILE_viofs_w11_amd64.sys.

A package is an OLE compound file; its files are in a cabinet stored as
one of its streams. libarchive, and so CMake, reads neither the compound
file nor this cabinet, and 7-Zip or msitools would be one more thing a
build needs, so both are read here: the compound file far enough to find
the stream that is a cabinet, and the cabinet's MSZIP folders, which are
deflate blocks zlib inflates. Cabinets compressed any other way (LZX,
Quantum) are refused, not guessed at.

The package is checked against its pinned SHA-256 before this runs, and
every file this writes against its own pinned hash after, so a reader
that went wrong cannot ship a wrong driver.
"""

import os
import struct
import sys
import zlib

CFB_MAGIC = bytes.fromhex("d0cf11e0a1b11ae1")
END_OF_CHAIN = 0xFFFFFFFA  # this and above end a sector chain
MSZIP = 1


def fail(message):
    sys.exit("msi_extract: " + message)


class CompoundFile:
    """The streams of an OLE compound file, read whole."""

    def __init__(self, data):
        if data[:8] != CFB_MAGIC:
            fail("not an OLE compound file")
        self.data = data
        self.sector = 1 << struct.unpack_from("<H", data, 30)[0]
        (fat_sectors, self.directory_start, _, self.cutoff, _, _, difat_start,
         _) = struct.unpack_from("<8I", data, 44)
        difat = list(struct.unpack_from("<109I", data, 76))
        per_sector = self.sector // 4
        sector = difat_start
        while sector < END_OF_CHAIN:
            values = struct.unpack_from("<%dI" % per_sector, data, self.offset(sector))
            difat.extend(values[:-1])
            sector = values[-1]
        self.fat = []
        for sector in difat[:fat_sectors]:
            self.fat.extend(struct.unpack_from("<%dI" % per_sector, data, self.offset(sector)))

    def offset(self, sector):
        return 512 + sector * self.sector

    def chain(self, start, size):
        out = bytearray()
        sector = start
        while sector < END_OF_CHAIN and len(out) < size:
            out += self.data[self.offset(sector):self.offset(sector) + self.sector]
            sector = self.fat[sector]
        return bytes(out[:size])

    def large_streams(self):
        """The streams held in whole sectors: every one a cabinet could be."""
        directory = self.chain(self.directory_start, len(self.data))
        for at in range(0, len(directory) - 127, 128):
            entry = directory[at:at + 128]
            if entry[66] != 2:  # not a stream
                continue
            start, size = struct.unpack_from("<IQ", entry, 116)
            if size >= self.cutoff:
                yield self.chain(start, size)


def cabinet_files(cab):
    """{name: bytes} for every file of an MSZIP cabinet."""
    (signature, _, _, _, files_at, _, _, _, folder_count, file_count, flags, _,
     _) = struct.unpack_from("<4sIIIIIBBHHHHH", cab, 0)
    if signature != b"MSCF":
        fail("the stream is not a cabinet")
    at = 36
    folder_reserve = data_reserve = 0
    if flags & 0x4:
        header_reserve, folder_reserve, data_reserve = struct.unpack_from("<HBB", cab, at)
        at += 4 + header_reserve
    for flag in (0x1, 0x2):  # a previous or next cabinet: two strings each
        if flags & flag:
            for _ in range(2):
                at = cab.index(b"\0", at) + 1
    if flags & 0x3:
        fail("the cabinet spans several files")

    folders = []
    for _ in range(folder_count):
        data_at, blocks, compression = struct.unpack_from("<IHH", cab, at)
        at += 8 + folder_reserve
        if compression & 0x0F != MSZIP:
            fail("the cabinet is compressed with type %d, not MSZIP" % (compression & 0x0F))
        folders.append((data_at, blocks))

    entries = []
    at = files_at
    for _ in range(file_count):
        size, offset, folder = struct.unpack_from("<IIH", cab, at)
        end = cab.index(b"\0", at + 16)
        entries.append((cab[at + 16:end].decode("utf-8", "replace"), size, offset, folder))
        at = end + 1

    inflated = {}

    def folder_bytes(index):
        if index not in inflated:
            data_at, blocks = folders[index]
            out = bytearray()
            for _ in range(blocks):
                _, packed, _ = struct.unpack_from("<IHH", cab, data_at)
                block = cab[data_at + 8 + data_reserve:data_at + 8 + data_reserve + packed]
                data_at += 8 + data_reserve + packed
                if block[:2] != b"CK":
                    fail("an MSZIP block without its CK signature")
                # Each block is a deflate stream of its own whose window
                # is the 32 KiB inflated before it.
                inflater = zlib.decompressobj(-15, zdict=bytes(out[-32768:]))
                out += inflater.decompress(block[2:]) + inflater.flush()
            inflated[index] = bytes(out)
        return inflated[index]

    return {name: folder_bytes(folder)[offset:offset + size]
            for name, size, offset, folder in entries if folder < len(folders)}


def main(argv):
    if len(argv) < 4:
        fail("usage: msi_extract.py PACKAGE.msi OUT_DIR KEY=NAME [KEY=NAME ...]")
    package, out_dir, pairs = argv[1], argv[2], argv[3:]
    with open(package, "rb") as handle:
        compound = CompoundFile(handle.read())
    files = {}
    for stream in compound.large_streams():
        if stream[:4] == b"MSCF":
            files.update(cabinet_files(stream))
    if not files:
        fail(package + " carries no cabinet")
    os.makedirs(out_dir, exist_ok=True)
    for pair in pairs:
        key, _, name = pair.partition("=")
        if key not in files:
            fail("%s has no %s" % (package, key))
        with open(os.path.join(out_dir, name or key), "wb") as handle:
            handle.write(files[key])


if __name__ == "__main__":
    main(sys.argv)
