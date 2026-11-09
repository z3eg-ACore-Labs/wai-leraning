"""Foreground optical addon/session matching. Reads only 96 marker pixels; never sends input."""
import ctypes
from ctypes import wintypes as W
import re

from client_probe import validate_bindings


def decode_marker(bits):
    if len(bits) != 96 or any(type(bit) is not int or bit not in (0,1) for bit in bits):
        return None
    def number(start, count):
        result = 0
        for bit in bits[start:start+count]: result = (result << 1) | bit
        return result
    if number(0,16) != 0xA6D3:
        return None
    raw = bytes(number(16+i*8,8) for i in range(8))
    if number(80,16) != sum((i+1)*value for i,value in enumerate(raw)) % 65536:
        return None
    return raw.hex()


def match_client(packet, marker, require_dungeon=True):
    if packet.get('schema') != 1 or packet.get('backend') != 'human_client_discovery':
        raise ValueError('Invalid identity discovery response')
    if not isinstance(marker,str) or not re.fullmatch('[0-9a-f]{16}',marker):
        return None
    rows=packet.get('clients')
    if not isinstance(rows,list) or len(rows)>64:
        raise ValueError('Invalid identity discovery list')
    matches=[row for row in rows if isinstance(row,dict) and row.get('marker')==marker]
    if len(matches)!=1:
        return None
    row=matches[0]
    if (not isinstance(row.get('player_guid'),str) or not row['player_guid'].isdigit() or
            type(row.get('in_dungeon')) is not bool or (require_dungeon and not row['in_dungeon'])):
        return None
    try:
        validate_bindings(row.get('bindings'))
    except (ValueError,TypeError):
        return None
    return row


class ForegroundMarker:
    def __init__(self):
        self.user=ctypes.WinDLL('user32',use_last_error=True)
        self.gdi=ctypes.WinDLL('gdi32',use_last_error=True)
        # Coordinate conversion and marker offsets are physical pixels on mixed-DPI desktops.
        self.user.SetProcessDpiAwarenessContext.argtypes=[W.HANDLE]
        self.user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        self.user.GetForegroundWindow.restype=W.HWND
        self.user.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)]
        self.user.ClientToScreen.argtypes=[W.HWND,ctypes.POINTER(W.POINT)]
        self.user.GetClientRect.argtypes=[W.HWND,ctypes.POINTER(W.RECT)]
        self.user.GetDC.argtypes=[W.HWND];self.user.GetDC.restype=W.HDC
        self.user.ReleaseDC.argtypes=[W.HWND,W.HDC]
        self.gdi.GetPixel.argtypes=[W.HDC,ctypes.c_int,ctypes.c_int];self.gdi.GetPixel.restype=W.DWORD

    def read(self, candidates):
        window=self.user.GetForegroundWindow();pid=W.DWORD()
        self.user.GetWindowThreadProcessId(window,ctypes.byref(pid))
        if not window or pid.value not in candidates:
            return None,None
        origin=W.POINT(0,0);rect=W.RECT()
        if (not self.user.ClientToScreen(window,ctypes.byref(origin)) or
                not self.user.GetClientRect(window,ctypes.byref(rect)) or rect.right<80 or rect.bottom<40):
            return pid.value,None
        dc=self.user.GetDC(None)
        if not dc:return pid.value,None
        bits=[]
        try:
            for index in range(96):
                color=self.gdi.GetPixel(dc,origin.x+8+(index%16)*4+2,origin.y+8+(index//16)*4+2)
                channels=[(color>>shift)&255 for shift in (0,8,16)]
                if color==0xffffffff or not (max(channels)<=64 or min(channels)>=192):
                    return pid.value,None
                bits.append(1 if min(channels)>=192 else 0)
        finally:
            self.user.ReleaseDC(None,dc)
        if self.user.GetForegroundWindow()!=window:
            return None,None
        return pid.value,decode_marker(bits)


_reader=None
def read_foreground_marker(candidates):
    global _reader
    if _reader is None:_reader=ForegroundMarker()
    return _reader.read(candidates)
