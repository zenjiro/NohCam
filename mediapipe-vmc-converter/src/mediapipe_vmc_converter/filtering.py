from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np


@dataclass
class OneEuroParams:
    frequency: float
    mincutoff: float
    beta: float
    dcutoff: float


class _LowPass:
    def __init__(self) -> None:
        self._initialized = False
        self._state = 0.0

    def filter(self, value: float, alpha: float) -> float:
        if not self._initialized:
            self._state = value
            self._initialized = True
            return value
        self._state = alpha * value + (1.0 - alpha) * self._state
        return self._state


class OneEuroFilter:
    def __init__(self, params: OneEuroParams) -> None:
        self.params = params
        self._x = _LowPass()
        self._dx = _LowPass()

    @staticmethod
    def _alpha(dt: float, cutoff: float) -> float:
        tau = 1.0 / (2.0 * math.pi * cutoff)
        return 1.0 / (1.0 + tau / dt)

    def filter(self, value: float, dt: float) -> float:
        if dt <= 0:
            return value
        dx = value - self._x._state if self._x._initialized else 0.0
        edx = self._dx.filter(dx / dt, self._alpha(dt, self.params.dcutoff))
        cutoff = self.params.mincutoff + self.params.beta * abs(edx)
        return self._x.filter(value, self._alpha(dt, cutoff))


class LandmarkFilterBank:
    def __init__(self, point_count: int, params: OneEuroParams) -> None:
        self.filters = [[OneEuroFilter(params) for _ in range(3)] for _ in range(point_count)]

    def filter_points(self, points: np.ndarray, dt: float) -> np.ndarray:
        out = points.copy()
        for i in range(points.shape[0]):
            for axis in range(3):
                out[i, axis] = self.filters[i][axis].filter(float(points[i, axis]), dt)
        return out
