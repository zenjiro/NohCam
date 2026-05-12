import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))


from nohcam.app import ManualModelTransform  # noqa: E402


class DummyModel:
    def __init__(self):
        self.scale = None
        self.offset = None

    def SetScale(self, scale):
        self.scale = scale

    def SetOffset(self, x, y):
        self.offset = (x, y)


class ManualModelTransformTest(unittest.TestCase):
    def test_zoom_scales_up_and_down(self):
        transform = ManualModelTransform()

        transform.zoom(1.0)
        up_scale = transform.scale
        transform.zoom(-1.0)

        self.assertGreater(up_scale, 1.0)
        self.assertLess(transform.scale, up_scale)

    def test_move_by_pixels_updates_vertical_offset(self):
        transform = ManualModelTransform()

        transform.move_by_pixels(-50)
        moved_up = transform.offset_y
        transform.move_by_pixels(100)

        self.assertGreater(moved_up, 0.0)
        self.assertLess(transform.offset_y, moved_up)

    def test_reset_restores_defaults(self):
        transform = ManualModelTransform(scale=2.0, offset_y=0.5, dragging=True)

        transform.reset()

        self.assertEqual(transform.scale, 1.0)
        self.assertEqual(transform.offset_y, 0.0)
        self.assertFalse(transform.dragging)

    def test_apply_writes_model_transform(self):
        transform = ManualModelTransform(scale=1.5, offset_y=-0.25)
        model = DummyModel()

        transform.apply(model)

        self.assertEqual(model.scale, 1.5)
        self.assertEqual(model.offset, (0.0, -0.25))


if __name__ == "__main__":
    unittest.main()
