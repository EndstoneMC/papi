"""PR2-001 / PR2-002 regression: GIL-safe legacy adapter destruction and native-gate ordering.

PR2-001: PythonLegacyExpansion must release its Python callback under the GIL using an
empty py::object (not py::none()). With py::none() the member destructor would dec_ref()
Py_None after the local GIL guard is destroyed, which is undefined on CPython 3.10/3.11
where Py_None is reference-counted. The failed-registration path exercises this without an
ambient GIL: the GIL is released before registerExpansion, and a rejected expansion is
destroyed before gil_scoped_release re-acquires the GIL.

PR2-002: registerPlaceholderFromPython must check callability with PyCallable_Check
(non-executing) so custom __getattribute__/descriptor machinery cannot run before the
native gate rejects an inactive or off-thread caller. An isActive() pre-gate rejects an
inactive service before the adapter is constructed.
"""

from __future__ import annotations

import gc
import weakref
from typing import ClassVar

from endstone_papi._papi import _TestService

# --------------------------------------------------------------------------- #
# PR2-001: GIL-safe legacy adapter destruction
# --------------------------------------------------------------------------- #


class FinalizerCounter:
    """A callable whose __del__ increments a class-level counter.

    The counter is reset in __init__ so each test starts from zero without cross-test
    coupling from finalizers that run at function teardown.
    """

    count = 0

    def __init__(self) -> None:
        FinalizerCounter.count = 0

    def __call__(self, player, params):
        return "value"

    def __del__(self) -> None:
        FinalizerCounter.count += 1


def test_failed_registration_releases_callback() -> None:
    """A duplicate registration fails; the rejected adapter's callback is released.

    The PythonLegacyExpansion is destroyed without an ambient GIL (the GIL was released
    before registerExpansion). clearCallbackUnderGil must acquire the GIL and release the
    callback ref so the finalizer runs exactly once.
    """
    host = _TestService("gil-test")

    first = FinalizerCounter()
    assert host.register_placeholder("dup", first) is True

    second = FinalizerCounter()
    assert host.register_placeholder("dup", second) is False

    # The Python variable still holds a ref, so the callback is not finalized yet.
    del second
    gc.collect()
    assert FinalizerCounter.count == 1, "rejected adapter's callback was not released"


def test_explicit_unregister_releases_callback() -> None:
    """Explicit unregister calls onUnregister, which clears the callback under the GIL."""
    host = _TestService("gil-test")

    cb = FinalizerCounter()
    assert host.register_placeholder("test", cb) is True
    assert host.unregister_expansion("test") is True

    # onUnregister cleared callback_ to an empty handle. The Python variable still holds
    # a ref, so the callback is not finalized yet.
    assert FinalizerCounter.count == 0

    del cb
    gc.collect()
    assert FinalizerCounter.count == 1, "callback was not released after unregister"


def test_bulk_unregister_releases_callback() -> None:
    """Bulk unregister calls onUnregister for each owned expansion."""
    host = _TestService("gil-test")

    cb = FinalizerCounter()
    assert host.register_placeholder("test", cb) is True
    assert host.unregister_expansions() == 1

    assert FinalizerCounter.count == 0

    del cb
    gc.collect()
    assert FinalizerCounter.count == 1, "callback was not released after bulk unregister"


def test_shutdown_releases_callback() -> None:
    """PAPI shutdown calls onUnregister(PapiShutdown), which clears the callback."""
    host = _TestService("gil-test")

    cb = FinalizerCounter()
    assert host.register_placeholder("test", cb) is True

    host.shutdown()
    # onUnregister(PapiShutdown) cleared callback_. The Python variable still holds a ref.
    assert FinalizerCounter.count == 0

    del cb
    gc.collect()
    assert FinalizerCounter.count == 1, "callback was not released after shutdown"


def test_destruction_after_onunregister_no_double_finalize() -> None:
    """After onUnregister clears callback_, the destructor must not release it again.

    This is the core of PR2-001: with the old py::none() assignment, callback_ would own
    Py_None after onUnregister, and the member destructor would dec_ref() it again without
    the GIL. With py::object() the member is empty, so the destructor is a no-op.
    """
    host = _TestService("gil-test")

    cb = FinalizerCounter()
    assert host.register_placeholder("test", cb) is True
    assert host.unregister_expansion("test") is True  # onUnregister clears callback_

    del cb
    gc.collect()
    assert FinalizerCounter.count == 1  # finalized once

    # Destroy the host. The PythonLegacyExpansion was already destroyed during unregister.
    # If callback_ had been left owning Py_None, the member destructor would dec_ref()
    # again. With the fix, callback_ is empty, so no second finalization.
    del host
    gc.collect()
    assert FinalizerCounter.count == 1, "callback was finalized a second time"


def test_weakref_dies_after_failed_registration() -> None:
    """A weakref to the rejected adapter's callback dies after the Python ref is dropped."""
    host = _TestService("gil-test")

    host.register_placeholder("dup", lambda p, a: "x")

    second = lambda p, a: "x"  # noqa: E731
    ref = weakref.ref(second)
    assert host.register_placeholder("dup", second) is False

    del second
    gc.collect()
    assert ref() is None, "rejected adapter held the callback alive"


# --------------------------------------------------------------------------- #
# PR2-002: native-gate ordering
# --------------------------------------------------------------------------- #


class AttributeCounter:
    """A callable whose __getattribute__ logs every access.

    PyCallable_Check reads the tp_call slot directly (C-level), so no __getattribute__ is
    invoked. The old py::hasattr(callback, "__call__") went through __getattribute__.
    """

    accesses: ClassVar[list[str]] = []

    def __getattribute__(self, name: str):
        AttributeCounter.accesses.append(name)
        return super().__getattribute__(name)

    def __call__(self, player, params):
        return "value"


def test_inactive_registration_zero_attribute_accesses() -> None:
    """After shutdown, register_placeholder does zero attribute accesses on the callback."""
    host = _TestService("gate-test")
    host.shutdown()
    assert not host.service.active

    cb = AttributeCounter()
    AttributeCounter.accesses.clear()

    result = host.register_placeholder("new", cb)

    assert result is False
    assert AttributeCounter.accesses == [], (
        f"attribute machinery executed before native gate: {AttributeCounter.accesses}"
    )


def test_inactive_registration_no_registry_mutation() -> None:
    """After shutdown, register_placeholder does not add anything to the registry."""
    host = _TestService("gate-test")
    host.shutdown()

    result = host.register_placeholder("new", lambda p, a: "x")

    assert result is False
    assert not host.service.is_registered("new")
    assert tuple(host.service.registered_identifiers) == ()


def test_successful_registration_zero_callback_attribute_accesses() -> None:
    """A successful registration also does zero attribute accesses on the callback.

    PyCallable_Check does not invoke __getattribute__. Metadata is read through the
    PythonLegacyExpansion's native virtual methods, not through the Python callback.
    """
    host = _TestService("gate-test")

    cb = AttributeCounter()
    AttributeCounter.accesses.clear()

    result = host.register_placeholder("ok", cb)

    assert result is True
    assert AttributeCounter.accesses == [], (
        f"attribute machinery executed during registration: {AttributeCounter.accesses}"
    )
