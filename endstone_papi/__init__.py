"""Endstone PlaceholderAPI."""

from ._papi import SERVICE_NAME, PlaceholderAPI, __version__
from .plugin import PlaceholderAPIPlugin

__all__ = ["SERVICE_NAME", "PlaceholderAPI", "PlaceholderAPIPlugin", "__version__"]
