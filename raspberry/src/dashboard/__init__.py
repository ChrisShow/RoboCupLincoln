"""Dashboard web di telemetria per il robot mENIAC (camere, sensori, motori, mappa)."""

from .camera import FakeCamera, SharedCamera
from .server import Dashboard

__all__ = ["Dashboard", "SharedCamera", "FakeCamera"]
