import io
import pathlib
import struct

SNAPPY_MAGIC = b"at"
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"
WIDENED = ("::ReclaimResources",)


class TraceError(Exception):
    pass


def snappy_decompress(data):
    pos, length, shift = 0, 0, 0
    while True:
        byte = data[pos]
        pos += 1
        length |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            break
    out = bytearray()
    while pos < len(data):
        tag = data[pos]
        pos += 1
        kind = tag & 3
        if kind == 0:
            size = tag >> 2
            if size >= 60:
                extra = size - 59
                size = int.from_bytes(data[pos:pos + extra], "little")
                pos += extra
            size += 1
            out += data[pos:pos + size]
            pos += size
            continue
        if kind == 1:
            size = ((tag >> 2) & 7) + 4
            offset = ((tag >> 5) << 8) | data[pos]
            pos += 1
        elif kind == 2:
            size = (tag >> 2) + 1
            offset = int.from_bytes(data[pos:pos + 2], "little")
            pos += 2
        else:
            size = (tag >> 2) + 1
            offset = int.from_bytes(data[pos:pos + 4], "little")
            pos += 4
        if offset == 0 or offset > len(out):
            raise TraceError("snappy: a copy reaches before the block")
        start = len(out) - offset
        for index in range(size):
            out.append(out[start + index])
    if len(out) != length:
        raise TraceError(f"snappy: block says {length} bytes, holds {len(out)}")
    return bytes(out)


def read_container(path):
    data = pathlib.Path(path).read_bytes()
    if data.startswith(SNAPPY_MAGIC):
        out, pos = io.BytesIO(), 2
        while pos < len(data):
            (size,) = struct.unpack_from("<I", data, pos)
            out.write(snappy_decompress(data[pos + 4:pos + 4 + size]))
            pos += 4 + size
        return out.getvalue()
    if data.startswith(ZSTD_MAGIC):
        from compression import zstd
        return zstd.decompress(data)
    raise TraceError(f"{path}: neither apitrace's snappy nor zstd container")


TAIL_PADDING = 4 << 20


def write_container(path, stream):
    from compression import zstd
    compressor = zstd.ZstdCompressor(level=6)
    with open(path, "wb") as out:
        for start in range(0, len(stream), 8 << 20):
            out.write(compressor.compress(stream[start:start + (8 << 20)]))
        out.write(compressor.flush())
        out.write(struct.pack("<II", 0x184D2A50, TAIL_PADDING) + bytes(TAIL_PADDING))


class Stream:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.calls = {}
        self.structs = {}
        self.enums = set()
        self.bitmasks = set()
        self.frames = set()

    def byte(self):
        value = self.data[self.pos]
        self.pos += 1
        return value

    def uint(self):
        value, shift, data, pos = 0, 0, self.data, self.pos
        while True:
            byte = data[pos]
            pos += 1
            value |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                self.pos = pos
                return value

    def string(self):
        size = self.uint()
        self.pos += size
        return self.data[self.pos - size:self.pos]

    def sint(self):
        kind = self.byte()
        if kind not in (3, 4):
            raise TraceError(f"offset {self.pos - 1}: a signed integer of type {kind}")
        value = self.uint()
        return -value if kind == 3 else value

    def header(self):
        version = self.uint()
        if version != 6:
            raise TraceError(f"trace format version {version}: only 6 (apitrace 10 and later) is read")
        self.uint()
        while self.string():
            self.string()

    def value(self):
        kind = self.byte()
        if kind in (0, 1, 2):
            return (None, False, True)[kind]
        if kind == 3:
            return -self.uint()
        if kind == 4:
            return self.uint()
        if kind == 5:
            self.pos += 4
        elif kind == 6:
            self.pos += 8
        elif kind in (7, 8):
            self.string()
        elif kind == 9:
            sig = self.uint()
            if sig not in self.enums:
                self.enums.add(sig)
                for _ in range(self.uint()):
                    self.string()
                    self.sint()
            return self.sint()
        elif kind == 10:
            sig = self.uint()
            if sig not in self.bitmasks:
                self.bitmasks.add(sig)
                for _ in range(self.uint()):
                    self.string()
                    self.uint()
            return self.uint()
        elif kind == 11:
            for _ in range(self.uint()):
                self.value()
        elif kind == 12:
            sig = self.uint()
            if sig not in self.structs:
                self.string()
                count = self.uint()
                for _ in range(count):
                    self.string()
                self.structs[sig] = count
            for _ in range(self.structs[sig]):
                self.value()
        elif kind == 13:
            self.uint()
        elif kind == 14:
            self.value()
            self.value()
        elif kind == 15:
            for _ in range(self.uint()):
                self.uint()
        else:
            raise TraceError(f"offset {self.pos - 1}: unknown value type {kind}")
        return None

    def function(self):
        sig = self.uint()
        if sig not in self.calls:
            name = bytes(self.string()).decode("utf-8", "replace")
            arguments = tuple(bytes(self.string()).decode("utf-8", "replace") for _ in range(self.uint()))
            self.calls[sig] = (name, arguments)
        return self.calls[sig]

    def backtrace(self):
        for _ in range(self.uint()):
            frame = self.uint()
            if frame in self.frames:
                continue
            self.frames.add(frame)
            while True:
                detail = self.byte()
                if detail == 0:
                    break
                if detail in (1, 2, 3):
                    self.string()
                elif detail in (4, 5):
                    self.uint()
                else:
                    raise TraceError(f"offset {self.pos - 1}: unknown backtrace detail {detail}")


class Mender:
    def __init__(self, data):
        self.data = data
        self.stream = Stream(data)
        self.pieces = []
        self.copied = 0
        self.mended = []
        self.pending = {}
        self.calls = 0
        self.open = {}
        self.left = 0
        self.dropped = []

    def run(self):
        stream = self.stream
        stream.header()
        end = len(self.data)
        while stream.pos < end:
            start = stream.pos
            try:
                self.event()
            except IndexError:
                end = start
                break
        trailing = sorted((offset, number, name) for number, (offset, name) in self.open.items()
                          if offset >= self.left)
        if trailing:
            end = min(end, trailing[0][0])
            self.dropped = [(number, name) for offset, number, name in trailing if offset >= end]
        if not self.mended and end == len(self.data):
            return self.data
        if end < self.copied:
            raise TraceError("a mended call lies past the end the trace was cut to")
        self.pieces.append(self.data[self.copied:end])
        return b"".join(self.pieces)

    def event(self):
        stream = self.stream
        kind = stream.byte()
        start = stream.pos - 1
        if kind == 0:
            stream.uint()
            name, arguments = stream.function()
            number, count, widened = self.calls, 0, name.endswith(WIDENED)
            self.open[number] = (start, name)
            self.calls += 1
        elif kind == 1:
            number = stream.uint()
            self.open.pop(number, None)
            name, arguments, count = self.pending.pop(number, (None, (), 0))
            widened = name is not None
        else:
            raise TraceError(f"offset {stream.pos - 1}: unknown event {kind}")
        while True:
            detail = stream.byte()
            if detail == 0:
                if kind == 1:
                    self.left = stream.pos
                return
            if detail == 1:
                arg = stream.uint()
                start = stream.pos
                value = stream.value()
                if not widened or arg >= len(arguments):
                    continue
                if kind == 0 and arguments[arg] == "NumResources" and isinstance(value, int):
                    count = value
                    self.pending[number] = (name, arguments, count)
                elif arguments[arg] == "pDiscarded" and count > 1:
                    self.widen(start, number, name, count)
            elif detail == 2:
                stream.value()
            elif detail in (3, 5):
                stream.uint()
            elif detail == 4:
                stream.backtrace()
            else:
                raise TraceError(f"offset {stream.pos - 1}: unknown call detail {detail}")

    def widen(self, start, number, name, count):
        data, end = self.data, self.stream.pos
        if data[start] != 11 or data[start + 1] != 1:
            return
        first = bytes(data[start + 2:end])
        if first[0] in (1, 2):
            again = first
        elif first[0] == 9:
            element = Stream(data)
            element.pos = start + 3
            sig = element.uint()
            if element.pos + sint_bytes(data, element.pos) != end:
                for _ in range(element.uint()):
                    element.string()
                    element.sint()
            again = bytes([9]) + encode_uint(sig) + bytes(data[element.pos:end])
        else:
            return
        self.pieces += [data[self.copied:start], bytes([11]) + encode_uint(count) + first + again * (count - 1)]
        self.copied = end
        self.mended.append((number, name, count))


def sint_bytes(data, pos):
    if data[pos] not in (3, 4):
        return -1
    length = 1
    while data[pos + length] & 0x80:
        length += 1
    return length + 1


def mend(data):
    mender = Mender(data)
    return mender.run(), mender


def encode_uint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def mend_file(path):
    data = read_container(path)
    mended_stream, mender = mend(memoryview(data))
    if mender.mended or mender.dropped:
        target = pathlib.Path(path)
        scratch = target.with_name(target.name + ".mending")
        write_container(scratch, mended_stream)
        scratch.replace(target)
    return mender
