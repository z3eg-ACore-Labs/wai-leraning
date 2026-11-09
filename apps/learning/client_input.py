"""Windows-only foreground input guard. Importing this module never loads hooks or sends input."""
import ctypes
from ctypes import wintypes as W
import threading
import time
import queue

TAG = 0x57414943


class GuardedInput:
    """A bounded key hold; stop/focus/manual-input watchdog runs independently of network IO."""
    def __init__(self, api, clock=time.monotonic):
        self.api, self.clock = api, clock
        self.lock = threading.RLock()
        self.held = set()
        self.deadline = 0
        self.armed = False
        self.arm_allowed = False
        self.reason = None

    def arm(self):
        with self.lock:
            if self.arm_allowed and self.api.focused() and not self.reason:
                self.armed = True

    def stop(self, reason):
        with self.lock:
            self.reason = self.reason or reason
            self.armed = False
            self._release()

    def _release(self):
        failures = []
        for key in tuple(self.held):
            try:
                self.api.key(key, False)
                self.held.remove(key)
            except Exception as error:
                failures.append(error)
        if failures:
            self.reason = self.reason or 'input_release_failed'
            self.armed = False
            raise RuntimeError('Unable to release a held movement key') from failures[0]

    def pulse(self, keys, seconds):
        if not 0 < seconds <= 1:
            raise ValueError('Movement pulses must be bounded to one second')
        with self.lock:
            self.tick()
            if not self.armed or self.reason:
                raise RuntimeError('Input is not armed')
            self._release()
            self.deadline = self.clock() + seconds
            try:
                for key in keys:
                    if not self.api.focused():
                        raise RuntimeError('Target client lost focus')
                    # Track first so even a partially successful native call gets a key-up.
                    self.held.add(key)
                    self.api.key(key, True)
            except Exception:
                self.stop('input_failed')
                raise

    def tick(self):
        with self.lock:
            if self.armed and not self.api.focused():
                self.stop('focus_lost')
            elif self.held and self.clock() >= self.deadline:
                self._release()


class WindowsInput:
    """Hooks only monitor events. The only generated events are configured movement keys."""
    def __init__(self, pid, controls_only=False, read_only=False):
        import os
        if os.name != 'nt':
            raise RuntimeError('Full-client input requires Windows')
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.pid = pid
        self.controls_only = controls_only
        self.read_only = read_only
        self.input_revision = 0
        self.injected_until = 0.0
        self.recording_paused = False
        self.user.GetForegroundWindow.restype = W.HWND
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD)]
        handle = self.kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            raise RuntimeError('Cannot inspect target process')
        try:
            text, size = ctypes.create_unicode_buffer(32768), W.DWORD(32768)
            if not self.kernel.QueryFullProcessImageNameW(handle, 0, text, ctypes.byref(size)):
                raise RuntimeError('Cannot verify client executable')
            if text.value.replace('\\', '/').rsplit('/', 1)[-1].lower() != 'wow.exe':
                raise ValueError('Target process must be Wow.exe')
        finally:
            self.kernel.CloseHandle(handle)
        self.guard = GuardedInput(self)
        self.done = threading.Event()
        self.stop_requested = threading.Event()
        self.controls = queue.SimpleQueue()
        self.ready = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self._watch, daemon=True, name='wai-client-input-watchdog')
        self.thread.start()
        if not self.ready.wait(3) or self.error:
            self.done.set()
            raise RuntimeError('Input watchdog could not start') from self.error

    def focused(self):
        window = self.user.GetForegroundWindow()
        pid = W.DWORD()
        self.user.GetWindowThreadProcessId(window, ctypes.byref(pid))
        return bool(window and pid.value == self.pid) and not self.stop_requested.is_set()

    def take_controls(self):
        result = set()
        while True:
            try:
                result.add(self.controls.get_nowait())
            except queue.Empty:
                return result

    def key(self, vk, down):
        if self.read_only:
            raise RuntimeError('Passive observation cannot generate input')
        class Keyboard(ctypes.Structure):
            _fields_ = [('vk', W.WORD), ('scan', W.WORD), ('flags', W.DWORD),
                        ('time', W.DWORD), ('extra', ctypes.c_size_t)]
        class Mouse(ctypes.Structure):
            _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('data', W.DWORD),
                        ('flags', W.DWORD), ('time', W.DWORD), ('extra', ctypes.c_size_t)]
        class Data(ctypes.Union):
            _fields_ = [('keyboard', Keyboard), ('mouse', Mouse)]
        class Input(ctypes.Structure):
            _fields_ = [('type', W.DWORD), ('data', Data)]
        value = Input()
        value.type = 1
        value.data.keyboard = Keyboard(vk, 0, 0 if down else 2, 0, TAG)
        self.user.SendInput.argtypes = [W.UINT, ctypes.POINTER(Input), ctypes.c_int]
        if self.user.SendInput(1, ctypes.byref(value), ctypes.sizeof(Input)) != 1:
            raise RuntimeError('Windows rejected a movement input event')

    def _watch(self):
        callback_type = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, ctypes.c_int, W.WPARAM, W.LPARAM)
        class KeyboardEvent(ctypes.Structure):
            _fields_ = [('vk', W.DWORD), ('scan', W.DWORD), ('flags', W.DWORD),
                        ('time', W.DWORD), ('extra', ctypes.c_size_t)]
        class MouseEvent(ctypes.Structure):
            _fields_ = [('point', W.POINT), ('data', W.DWORD), ('flags', W.DWORD),
                        ('time', W.DWORD), ('extra', ctypes.c_size_t)]
        self.user.SetWindowsHookExW.argtypes = [ctypes.c_int, callback_type, W.HINSTANCE, W.DWORD]
        self.user.SetWindowsHookExW.restype = W.HANDLE
        self.user.CallNextHookEx.argtypes = [W.HANDLE, ctypes.c_int, W.WPARAM, W.LPARAM]
        self.user.CallNextHookEx.restype = ctypes.c_ssize_t
        self.user.UnhookWindowsHookEx.argtypes = [W.HANDLE]
        self.kernel.GetModuleHandleW.argtypes = [W.LPCWSTR]
        self.kernel.GetModuleHandleW.restype = W.HMODULE
        hooks = []
        mode_held = set()
        def keyboard(code, message, data):
            try:
                if code >= 0 and message in (0x100, 0x104, 0x101, 0x105):
                    event = ctypes.cast(data, ctypes.POINTER(KeyboardEvent)).contents
                    self.input_revision += 1
                    if event.flags & 0x10 or event.extra == TAG:
                        self.injected_until = time.monotonic() + 1.0
                        if self.read_only:
                            self.recording_paused = True
                    if self.read_only and self.focused() and message in (0x100, 0x104):
                        if event.vk in (0x0D, 0x1B, 0xBF):
                            self.recording_paused = True
                    if event.extra != TAG:
                        if message in (0x101, 0x105):
                            mode_held.discard(event.vk)
                            return self.user.CallNextHookEx(None, code, message, data)
                        if event.vk in (0x75, 0x76, 0x77, 0x78, 0x79) and self.focused():
                            if event.vk not in mode_held:
                                self.controls.put(event.vk)
                            mode_held.add(event.vk)
                        if event.vk == 0x7B:  # F12
                            self.stop_requested.set()
                            self.guard.stop('emergency_stop')
                        elif event.vk == 0x77 and not self.controls_only:  # Legacy probe arm
                            self.guard.arm()
                        elif self.guard.armed:
                            self.guard.stop('manual_keyboard_input')
            except Exception as error:
                self.error = error
                self.done.set()
            return self.user.CallNextHookEx(None, code, message, data)
        def mouse(code, message, data):
            try:
                if code >= 0 and message in (0x201, 0x204, 0x207, 0x20A, 0x20B, 0x20E):
                    event = ctypes.cast(data, ctypes.POINTER(MouseEvent)).contents
                    self.input_revision += 1
                    if event.flags & 1 or event.extra == TAG:
                        self.injected_until = time.monotonic() + 1.0
                        if self.read_only:
                            self.recording_paused = True
                    if event.extra != TAG and self.guard.armed:
                        self.guard.stop('manual_mouse_input')
            except Exception as error:
                self.error = error
                self.done.set()
            return self.user.CallNextHookEx(None, code, message, data)
        callbacks = [callback_type(keyboard), callback_type(mouse)]
        try:
            for hook_type, callback in zip((13, 14), callbacks):
                handle = self.user.SetWindowsHookExW(hook_type, callback, self.kernel.GetModuleHandleW(None), 0)
                if not handle:
                    raise RuntimeError('Cannot register input takeover monitor')
                hooks.append(handle)
            self.ready.set()
            msg = W.MSG()
            while not self.done.is_set():
                while self.user.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
                    self.user.TranslateMessage(ctypes.byref(msg))
                    self.user.DispatchMessageW(ctypes.byref(msg))
                self.guard.tick()
                time.sleep(.01)
        except Exception as error:
            self.error = error
        finally:
            self.ready.set()
            try:
                self.guard.stop('watchdog_stopped')
            finally:
                for hook in hooks:
                    self.user.UnhookWindowsHookEx(hook)

    def close(self):
        self.done.set()
        self.thread.join(timeout=2)
        self.guard.stop('controller_stopped')
