"""Borrowed-handle, read-only Windows transport. No PID discovery/open/write API.

Does not load any target DLL, resolve imports, pin a module or acquire a debugger
stop. Exact before/after metadata and process checks are not a lease/ABA proof.
"""
import ctypes as ct
from dataclasses import dataclass
import os

from p99_wrist_mapped_image import _address, PAGE_READONLY, PAGE_EXECUTE_READ
from p99_wrist_abi_manifest import require


class MBI(ct.Structure):
    _fields_ = [('BaseAddress', ct.c_void_p), ('AllocationBase', ct.c_void_p),
                ('AllocationProtect', ct.c_uint32), ('PartitionId', ct.c_uint16),
                ('RegionSize', ct.c_size_t), ('State', ct.c_uint32),
                ('Protect', ct.c_uint32), ('Type', ct.c_uint32)]


class FileTime(ct.Structure):
    _fields_ = [('low', ct.c_uint32), ('high', ct.c_uint32)]


@dataclass(frozen=True)
class Identity:
    pid: int
    creation: int


class WindowsAPI:
    def __init__(self):
        require(os.name == 'nt' and ct.sizeof(ct.c_void_p) == 8 and ct.sizeof(MBI) == 48,
                '64-bit Windows required')
        self.kernel = ct.WinDLL('kernel32', use_last_error=True)
        for name, result, args in (
            ('VirtualQueryEx', ct.c_size_t, [ct.c_void_p, ct.c_void_p, ct.POINTER(MBI), ct.c_size_t]),
            ('ReadProcessMemory', ct.c_int, [ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.POINTER(ct.c_size_t)]),
            ('GetProcessId', ct.c_uint32, [ct.c_void_p]),
            ('WaitForSingleObject', ct.c_uint32, [ct.c_void_p, ct.c_uint32]),
            ('GetProcessTimes', ct.c_int, [ct.c_void_p]+[ct.POINTER(FileTime)]*4)):
            function = getattr(self.kernel, name)
            function.restype, function.argtypes = result, args

    def identity(self, handle):
        require(self.kernel.WaitForSingleObject(handle, 0) == 258, 'process no longer live')
        pid = self.kernel.GetProcessId(handle)
        times = [FileTime() for _ in range(4)]
        require(pid and self.kernel.GetProcessTimes(handle, *(ct.byref(value) for value in times)),
                'process identity query failed')
        return Identity(pid, times[0].low | (times[0].high << 32))

    def query(self, handle, address):
        result = MBI()
        require(self.kernel.VirtualQueryEx(handle, address, ct.byref(result), ct.sizeof(result)) == ct.sizeof(result),
                'incomplete memory query')
        return result

    def read(self, handle, address, size):
        buffer, actual = (ct.c_ubyte*size)(), ct.c_size_t()
        require(self.kernel.ReadProcessMemory(handle, address, buffer, size, ct.byref(actual)) and
                actual.value == size, 'incomplete memory read')
        return bytes(buffer)


class Reader:
    def __init__(self, borrowed_handle, identity, base, size, *, api=None):
        require(borrowed_handle and isinstance(identity, Identity) and identity.pid > 0 and
                identity.creation > 0 and _address(base, size) and base % 65536 == 0 and
                size % 4096 == 0 and size <= 64*1024*1024, 'reader identity/extent')
        self._handle, self._identity, self._base, self._size = borrowed_handle, identity, base, size
        self._api = WindowsAPI() if api is None else api  # Owned fault seam; no write hooks.

    def boundary(self, base, size):
        require(base == self._base and size == self._size, 'reader/plan mismatch')
        require(self._api.identity(self._handle) == self._identity, 'process identity changed')

    def _metadata(self, address, size, protection):
        cursor, end = address, address+size
        spans = []
        while cursor < end:
            require(len(spans) < 64, 'memory-query work bound')
            m = self._api.query(self._handle, cursor)
            first, allocation, count = m.BaseAddress or 0, m.AllocationBase or 0, m.RegionSize
            require(m.State == 0x1000 and m.Type == 0x1000000 and allocation == self._base and
                    m.Protect == protection, 'image state/allocation/protection mismatch')
            require(_address(first, count) and first <= cursor < first+count, 'memory region extent')
            spans.append((first, count, allocation, m.State, m.Type, m.Protect, m.AllocationProtect))
            cursor = min(end, first+count)
        return tuple(spans)

    def read(self, address, size, protection):
        require(_address(address, size) and 0 < size <= 65536 and
                self._base <= address and address+size <= self._base+self._size and
                protection in (PAGE_READONLY, PAGE_EXECUTE_READ), 'read range/protection')
        before = self._metadata(address, size, protection)
        data = self._api.read(self._handle, address, size)
        require(isinstance(data, bytes) and len(data) == size, 'short/nonbytes read')
        require(self._metadata(address, size, protection) == before, 'mapping metadata changed during read')
        return data
