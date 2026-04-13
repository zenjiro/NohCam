import wx
import cv2
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import mediapipe as mp


CameraWidth = 640
CameraHeight = 480
CameraFps = 30

ProcessWidth = 640
ProcessHeight = 480

WindowWidth = 960
WindowHeight = 720


HAND_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (5, 9), (9, 10), (10, 11), (11, 12),
    (9, 13), (13, 14), (14, 15), (15, 16),
    (13, 17), (17, 18), (18, 19), (19, 20),
]

POSE_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 7),
    (0, 4), (4, 5), (5, 6), (6, 8),
    (9, 10),
    (11, 12),
    (11, 13), (13, 15), (15, 17), (15, 19), (15, 21), (17, 19),
    (12, 14), (14, 16), (16, 18), (16, 20), (16, 22), (18, 20),
    (11, 23), (12, 24), (23, 24),
    (23, 25), (24, 26),
    (25, 27), (26, 28),
    (27, 29), (28, 30),
    (29, 31), (30, 32),
    (27, 31), (28, 32),
]

FACE_OUTLINE = [10, 338, 297, 332, 284, 328, 291, 324, 318, 196, 389, 394, 364, 292, 439, 276, 53, 412, 476, 356, 11]


class MainFrame(wx.Frame):
    def __init__(self):
        super().__init__(
            None,
            title="nohcam-tracker",
            size=wx.Size(WindowWidth, WindowHeight),
        )

        self.frame_count = 0

        self._setup_ui()
        self._setup_tracker()

    def _setup_ui(self):
        panel = wx.Panel(self)
        sizer = wx.BoxSizer(wx.VERTICAL)

        self.canvas = wx.Panel(panel, size=wx.Size(WindowWidth, WindowHeight))
        sizer.Add(self.canvas, proportion=1, flag=wx.EXPAND)

        panel.SetSizer(sizer)
        self.Layout()

        self.capture_timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self._on_timer, self.capture_timer)

    def _setup_tracker(self):
        base_options = python.BaseOptions(model_asset_path="models/holistic_landmarker.task")
        options = vision.HolisticLandmarkerOptions(
            base_options=base_options,
            running_mode=vision.RunningMode.VIDEO,
        )
        self.detector = vision.HolisticLandmarker.create_from_options(options)

        self.cap = cv2.VideoCapture(0)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, ProcessWidth)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, ProcessHeight)
        self.cap.set(cv2.CAP_PROP_FPS, CameraFps)

        self.current_frame = None
        self.latest_result = None

        self.capture_timer.Start(1000 // CameraFps)

    def _on_timer(self, event):
        ret, frame = self.cap.read()
        if not ret:
            return

        self.current_frame = frame.copy()
        self.frame_count += 1

        timestamp_ms = int(self.frame_count * 1000 / CameraFps)

        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
        self.latest_result = self.detector.detect_for_video(mp_image, timestamp_ms)

        wx.CallAfter(self._update_display)

    def _update_display(self):
        if self.current_frame is None:
            return

        frame = self.current_frame.copy()

        if self.latest_result:
            frame = self._draw_overlays(frame, self.latest_result)

        h, w = frame.shape[:2]
        bmp = wx.Bitmap.FromBufferRGBA(w, h, cv2.cvtColor(frame, cv2.COLOR_BGR2RGBA))

        cw, ch = self.canvas.GetSize()
        img = wx.ImageFromBitmap(bmp)
        img = img.Rescale(cw, ch)
        bmp = img.ConvertToBitmap()

        dc = wx.ClientDC(self.canvas)
        dc.Clear()
        dc.DrawBitmap(bmp, 0, 0)

    def _draw_overlays(self, frame, result):
        h, w = frame.shape[:2]

        if result.face_landmarks:
            face_pts = [(int(lm.x * w), int(lm.y * h)) for lm in result.face_landmarks]

            for i in range(len(face_pts) - 1):
                cv2.line(frame, face_pts[i], face_pts[i + 1], (0, 255, 0), 1)

            cv2.polylines(frame, [np.array([face_pts[i] for i in FACE_OUTLINE], np.int32)], True, (0, 255, 0), 2)

        if result.left_hand_landmarks:
            hand_pts = [(int(lm.x * w), int(lm.y * h)) for lm in result.left_hand_landmarks]
            self._draw_hand(frame, hand_pts)

        if result.right_hand_landmarks:
            hand_pts = [(int(lm.x * w), int(lm.y * h)) for lm in result.right_hand_landmarks]
            self._draw_hand(frame, hand_pts)

        if result.pose_landmarks:
            pose_pts = [(int(lm.x * w), int(lm.y * h)) for lm in result.pose_landmarks]
            self._draw_pose(frame, pose_pts)

        return frame

    def _draw_hand(self, frame, pts):
        for i, j in HAND_CONNECTIONS:
            if i < len(pts) and j < len(pts):
                cv2.line(frame, pts[i], pts[j], (0, 255, 0), 2)

        for pt in pts:
            cv2.circle(frame, pt, 3, (0, 0, 255), -1)

    def _draw_pose(self, frame, pts):
        for i, j in POSE_CONNECTIONS:
            if i < len(pts) and j < len(pts):
                cv2.line(frame, pts[i], pts[j], (0, 255, 0), 2)

        for pt in pts:
            cv2.circle(frame, pt, 3, (0, 0, 255), -1)

        nose_idx = [0, 1, 2, 3, 4, 5, 6]
        for idx in nose_idx:
            if idx < len(pts):
                cv2.circle(frame, pts[idx], 5, (255, 0, 0), -1)

    def OnClose(self, event):
        self.capture_timer.Stop()
        if self.cap:
            self.cap.release()
        self.Destroy()


def main():
    app = wx.App()
    frame = MainFrame()
    frame.Bind(wx.EVT_CLOSE, frame.OnClose)
    frame.Show()
    app.MainLoop()


if __name__ == "__main__":
    main()
