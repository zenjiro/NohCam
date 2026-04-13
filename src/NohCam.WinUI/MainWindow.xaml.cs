using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using WinRT.Interop;
using Windows.Devices.Enumeration;
using Windows.Foundation;
using Windows.Graphics.Imaging;
using Windows.Media.Capture;
using Windows.Media.Capture.Frames;
using Windows.Media.MediaProperties;
using Windows.Media.Playback;
using Windows.Storage.Streams;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading;

namespace NohCam.WinUI;

public sealed partial class MainWindow : Window
{
    private MediaCapture? _mediaCapture;
    private MediaPlayer? _mediaPlayer;
    private MediaFrameReader? _frameReader;

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_Initialize();

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FaceTracker_Shutdown();

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FaceTracker_GetInitError(System.Text.StringBuilder errorBuffer, int bufferSize);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadLibraryW(string lpFileName);

    private IntPtr _bridgeModule = IntPtr.Zero;

    private void LoadBridgeModule() {
        var exeDir = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
        var dllPath = Path.Combine(exeDir ?? ".", "nohcam_bridge.dll");
        Log($"LoadBridgeModule: loading {dllPath}");
        
        _bridgeModule = LoadLibraryW(dllPath);
        if (_bridgeModule == IntPtr.Zero) {
            var error = Marshal.GetLastWin32Error();
            Log($"LoadBridgeModule: failed with error {error}");
        } else {
            Log($"LoadBridgeModule: loaded successfully");
        }
    }

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_TrackAll(
        [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.U1)] byte[] pixels,
        uint width,
        uint height,
        uint stride);

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_GetFaceResult(
        out bool detected,
        out float yaw,
        out float pitch,
        out float roll,
        out float x,
        out float y,
        out int blendshapeCount,
        float[] blendshapes,
        int blendshapeCapacity);

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_GetPoseResult(
        out bool detected,
        out float score,
        float[] landmarks33xyz,
        float[] visibility33,
        float[] presence33);

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_GetHandResult(
        out bool leftDetected,
        out float leftWristX,
        out float leftWristY,
        out float leftWristZ,
        out bool rightDetected,
        out float rightWristX,
        out float rightWristY,
        out float rightWristZ);

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_GetHandLandmarks(bool left, float[] landmarks21xyz);

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_GetFaceLandmarks(float[] landmarks478xyz);

    private readonly float[] _poseLandmarks = new float[33 * 3];
    private readonly float[] _poseVisibility = new float[33];
    private readonly float[] _posePresence = new float[33];

    private readonly float[] _faceLandmarks = new float[478 * 3];
    private readonly float[] _leftHandLandmarks = new float[21 * 3];
    private readonly float[] _rightHandLandmarks = new float[21 * 3];

    private readonly float[] _blendshapes = new float[128];

    // Landmark connections for rendering
    private static readonly (int, int)[] HandConnections = new[] {
        (0, 1), (1, 2), (2, 3), (3, 4),
        (0, 5), (5, 6), (6, 7), (7, 8),
        (5, 9), (9, 10), (10, 11), (11, 12),
        (9, 13), (13, 14), (14, 15), (15, 16),
        (13, 17), (17, 18), (18, 19), (19, 20),
    };

    private static readonly (int, int)[] PoseConnections = new[] {
        (0, 1), (1, 2), (2, 3), (3, 7),
        (0, 4), (4, 5), (5, 6), (6, 8),
        (9, 10), (11, 12),
        (11, 13), (13, 15), (15, 17), (15, 19), (15, 21), (17, 19),
        (12, 14), (14, 16), (16, 18), (16, 20), (16, 22), (18, 20),
        (11, 23), (12, 24), (23, 24),
        (23, 25), (24, 26), (25, 27), (26, 28),
        (27, 29), (28, 30), (29, 31), (30, 32), (27, 31), (28, 32),
    };

    [DllImport("user32.dll")]
    private static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll")]
    private static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    private const int GWL_STYLE = -16;
    private const int WS_MAXIMIZE = 0x01000000;

    public MainWindow()
    {
        InitializeComponent();

        var hWnd = WindowNative.GetWindowHandle(this);
        var windowId = Win32Interop.GetWindowIdFromWindow(hWnd);
        var appWindow = AppWindow.GetFromWindowId(windowId);

        if (appWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.Maximize();
        }

        string exeDir = System.AppDomain.CurrentDomain.BaseDirectory;
    string dllPath = System.IO.Path.Combine(exeDir, "nohcam_bridge.dll");
    Console.WriteLine($"[DEBUG] App Directory: {exeDir}");
    Console.WriteLine($"[DEBUG] DLL Path: {dllPath}");
    Console.WriteLine($"[DEBUG] DLL exists: {System.IO.File.Exists(dllPath)}");

    InitLogger();
    Activated += OnActivated;
    Closed += OnClosed;
}
    private async void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        Activated -= OnActivated;

        StatusTextBlock.Text = "Initializing tracker...";
        Log($"OnActivated: initializing tracker, cwd={Environment.CurrentDirectory}");
        Log($"OnActivated: exe dir={System.Reflection.Assembly.GetExecutingAssembly().Location}");

        LoadBridgeModule();

        try {
            StatusTextBlock.Text = "Calling Initialize()...";
            var sw = System.Diagnostics.Stopwatch.StartNew();
            bool success = FaceTracker_Initialize();
            sw.Stop();
            StatusTextBlock.Text = $"Init took {sw.ElapsedMilliseconds}ms (success={success})";
            Log($"OnActivated: Initialize took {sw.ElapsedMilliseconds}ms success={success}");
            
            var errorBuilder = new System.Text.StringBuilder(1024);
            FaceTracker_GetInitError(errorBuilder, 1024);
            var error = errorBuilder.ToString();
            if (!string.IsNullOrEmpty(error)) {
                StatusTextBlock.Text = $"Init error: {error}";
                Log($"OnActivated: Init error: {error}");
            }
        } catch (Exception ex) {
            StatusTextBlock.Text = $"Exception: {ex.Message}";
            Log($"OnActivated: Exception: {ex.Message}");
        }

        await StartPreviewAsync();
    }

    private async Task StartPreviewAsync()
    {
        StatusTextBlock.Text = "Opening camera...";
        PreviewStateTextBlock.Text = "Preview: initializing";
        Log("StartPreviewAsync: begin");

        await StopPreviewAsync();

        try
        {
            var videoDevices = await DeviceInformation.FindAllAsync(DeviceClass.VideoCapture);
            var firstDevice = videoDevices.FirstOrDefault();
            if (firstDevice is null)
            {
                DeviceTextBlock.Text = "Device: not found";
                StatusTextBlock.Text = "No camera found.";
                PreviewStateTextBlock.Text = "Preview: unavailable";
                Log("StartPreviewAsync: no device");
                return;
            }

            DeviceTextBlock.Text = $"Device: {firstDevice.Name}";
            Log($"StartPreviewAsync: device={firstDevice.Name}");

            _mediaCapture = new MediaCapture();
            await _mediaCapture.InitializeAsync(new MediaCaptureInitializationSettings
            {
                VideoDeviceId = firstDevice.Id,
                StreamingCaptureMode = StreamingCaptureMode.Video,
                SharingMode = MediaCaptureSharingMode.ExclusiveControl,
                MemoryPreference = MediaCaptureMemoryPreference.Cpu
            });
            Log("StartPreviewAsync: MediaCapture initialized");

            // List all frame sources for debugging
            foreach (var fs in _mediaCapture.FrameSources.Values) {
                Log($"StartPreviewAsync: frameSource: kind={fs.Info.SourceKind} stream={fs.Info.MediaStreamType}");
            }

            // Try to find VideoPreview source first, then VideoRecord
            var frameSource = _mediaCapture.FrameSources.Values.FirstOrDefault(source =>
                source.Info.SourceKind == MediaFrameSourceKind.Color &&
                source.Info.MediaStreamType == MediaStreamType.VideoPreview);

            if (frameSource is null) {
                frameSource = _mediaCapture.FrameSources.Values.FirstOrDefault(source =>
                    source.Info.SourceKind == MediaFrameSourceKind.Color &&
                    source.Info.MediaStreamType == MediaStreamType.VideoRecord);
            }

            if (frameSource is null)
            {
                Log("StartPreviewAsync: no frame source");
                throw new InvalidOperationException("No color frame source is available.");
            }

            var selectedFormat = SelectHighFpsFormat(frameSource);
            if (selectedFormat is not null && frameSource.CurrentFormat != selectedFormat)
            {
                await frameSource.SetFormatAsync(selectedFormat);
                Log($"StartPreviewAsync: selected format {DescribeFormat(selectedFormat)}");
            }
            else
            {
                Log($"StartPreviewAsync: using current format {DescribeFormat(frameSource.CurrentFormat)}");
            }

            // FrameReader with default format for tracking
            _frameReader = await _mediaCapture.CreateFrameReaderAsync(frameSource);
            _frameReader.FrameArrived += FrameReader_FrameArrived;
            await _frameReader.StartAsync();
            Log("StartPreviewAsync: FrameReader started");

            // MediaPlayer for display
            _mediaPlayer = new MediaPlayer
            {
                RealTimePlayback = true,
                AutoPlay = false,
                Source = Windows.Media.Core.MediaSource.CreateFromMediaFrameSource(frameSource)
            };
            _mediaPlayer.MediaFailed += MediaPlayer_MediaFailed;
            PreviewElement.SetMediaPlayer(_mediaPlayer);
            PreviewPlaceholderTextBlock.Visibility = Visibility.Collapsed;
            _mediaPlayer.Play();
            Log("StartPreviewAsync: MediaPlayer started");
            Log("StartPreviewAsync: done");

            StatusTextBlock.Text = "Camera preview is running.";
            PreviewStateTextBlock.Text = "Preview: active (Tracking ON)";
            Log("StartPreviewAsync: complete");
        }
        catch (Exception ex)
        {
            StatusTextBlock.Text = "Camera preview failed.";
            PreviewStateTextBlock.Text = $"Preview: {ex.Message}";
            PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
            Log($"StartPreviewAsync: FAILED - {ex}");
        }
    }

    private int _frameCount = 0;
    private int _debugFrameCount = 0;
    private int _lastPixelSum = 0;
    private System.Diagnostics.Stopwatch _fpsTimer = System.Diagnostics.Stopwatch.StartNew();
    private int _fpsFrameCount = 0;
    private StreamWriter? _logWriter;
    private string _logPath = "";
    private readonly object _logLock = new object();
    private System.Diagnostics.Stopwatch _trackTimer = System.Diagnostics.Stopwatch.StartNew();
    private int _isTracking = 0;
    private readonly int _trackIntervalMs = 33; // Target ~30 fps tracking
    private byte[]? _trackingPixelBuffer;
    private System.Diagnostics.Stopwatch _trackFpsTimer = System.Diagnostics.Stopwatch.StartNew();
    private int _trackCallCount = 0;
    private int _handLogCounter = 0;

    private void InitLogger()
    {
        var exeDir = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
        _logPath = Path.Combine(exeDir ?? ".", "nohcam_debug.log");
        try {
            _logWriter = new StreamWriter(_logPath, false) { AutoFlush = true };
            Log($"=== NohCam started, log={_logPath} ===");
        } catch (Exception ex) {
            System.Diagnostics.Debug.WriteLine($"Failed to create log: {ex.Message}");
        }
    }

    private void Log(string msg)
    {
        System.Diagnostics.Debug.WriteLine(msg);
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] {msg}");
        lock (_logLock) {
            _logWriter?.WriteLine($"[{DateTime.Now:HH:mm:ss.fff}] {msg}");
        }
    }

    private double _videoWidth = 640;
    private double _videoHeight = 480;

    private void UpdateOverlay(bool faceOk, bool poseOk, bool leftOk, bool rightOk)
    {
        if (OverlayCanvas == null) return;

        double controlWidth = PreviewElement.ActualWidth;
        double controlHeight = PreviewElement.ActualHeight;
        if (controlWidth == 0 || controlHeight == 0) return;

        double scale = Math.Min(controlWidth / _videoWidth, controlHeight / _videoHeight);
        double actualW = _videoWidth * scale;
        double actualH = _videoHeight * scale;
        double offsetX = (controlWidth - actualW) / 2;
        double offsetY = (controlHeight - actualH) / 2;

        OverlayCanvas.Children.Clear();

        // Use mirroring for preview by default (selfie style)
        bool mirror = true;

        // 1. Pose
        if (poseOk)
        {
            foreach (var (i, j) in PoseConnections)
            {
                if (i < 33 && j < 33)
                    DrawLine(i, j, _poseLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Green, 2, mirror);
            }
            for (int i = 0; i < 33; i++)
                DrawPoint(i, _poseLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Red, 4, mirror);
        }

        // 2. Hands
        if (leftOk)
        {
            foreach (var (i, j) in HandConnections)
                DrawLine(i, j, _leftHandLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Green, 2, mirror);
            for (int i = 0; i < 21; i++)
                DrawPoint(i, _leftHandLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Red, 4, mirror);
        }
        if (rightOk)
        {
            foreach (var (i, j) in HandConnections)
                DrawLine(i, j, _rightHandLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Green, 2, mirror);
            for (int i = 0; i < 21; i++)
                DrawPoint(i, _rightHandLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Red, 4, mirror);
        }

        // 3. Face (standard MediaPipe mesh is 468 points, irises are 469-478)
        if (faceOk)
        {
            for (int i = 0; i < 468; i++)
                DrawPoint(i, _faceLandmarks, offsetX, offsetY, scale, Microsoft.UI.Colors.Red, 2, mirror);
        }
    }

    private void DrawLine(int idx1, int idx2, float[] landmarks, double ox, double oy, double scale, Windows.UI.Color color, double thickness, bool mirror)
    {
        float x1n = landmarks[idx1 * 3 + 0];
        float x2n = landmarks[idx2 * 3 + 0];
        if (mirror) { x1n = 1.0f - x1n; x2n = 1.0f - x2n; }

        double x1 = x1n * _videoWidth * scale + ox;
        double y1 = landmarks[idx1 * 3 + 1] * _videoHeight * scale + oy;
        double x2 = x2n * _videoWidth * scale + ox;
        double y2 = landmarks[idx2 * 3 + 1] * _videoHeight * scale + oy;

        var line = new Microsoft.UI.Xaml.Shapes.Line { X1 = x1, Y1 = y1, X2 = x2, Y2 = y2, Stroke = new SolidColorBrush(color), StrokeThickness = thickness };
        OverlayCanvas.Children.Add(line);
    }

    private void DrawPoint(int idx, float[] landmarks, double ox, double oy, double scale, Windows.UI.Color color, double size, bool mirror)
    {
        float xn = landmarks[idx * 3 + 0];
        if (mirror) xn = 1.0f - xn;

        double x = xn * _videoWidth * scale + ox;
        double y = landmarks[idx * 3 + 1] * _videoHeight * scale + oy;
        var dot = new Microsoft.UI.Xaml.Shapes.Ellipse { Width = size, Height = size, Fill = new SolidColorBrush(color) };
        Canvas.SetLeft(dot, x - size / 2); Canvas.SetTop(dot, y - size / 2);
        OverlayCanvas.Children.Add(dot);
    }

    private void FrameReader_FrameArrived(MediaFrameReader sender, MediaFrameArrivedEventArgs args)
    {
        _debugFrameCount++;
        _fpsFrameCount++;

        if (_debugFrameCount % 30 == 0) {
            var elapsed = _fpsTimer.ElapsedMilliseconds;
            double fps = elapsed > 0 ? _fpsFrameCount * 1000.0 / elapsed : 0;
            Log($"Frame #{_debugFrameCount}: FPS={fps:F1}");
        }

        if (_trackTimer.ElapsedMilliseconds < _trackIntervalMs) return;
        if (Interlocked.CompareExchange(ref _isTracking, 1, 0) != 0) return;
        _trackTimer.Restart();

        try
        {
            using var frame = sender.TryAcquireLatestFrame();
            if (frame?.VideoMediaFrame?.SoftwareBitmap is null) return;

            _videoWidth = frame.VideoMediaFrame.VideoFormat.Width;
            _videoHeight = frame.VideoMediaFrame.VideoFormat.Height;

            using var bitmap = SoftwareBitmap.Convert(frame.VideoMediaFrame.SoftwareBitmap, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore);
            int width = bitmap.PixelWidth;
            int height = bitmap.PixelHeight;
            int requiredSize = width * height * 4;

            if (_trackingPixelBuffer is null || _trackingPixelBuffer.Length != requiredSize)
            {
                _trackingPixelBuffer = new byte[requiredSize];
                Log($"Tracking buffer allocated: {requiredSize / (1024 * 1024)} MB");
            }

            bitmap.CopyToBuffer(_trackingPixelBuffer.AsBuffer());
            var pixelData = _trackingPixelBuffer;

            _ = Task.Run(() =>
            {
                try
                {
                    bool faceDetected = false, poseDetected = false, leftHandDetected = false, rightHandDetected = false;
                    float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0, poseScore = 0;
                    int blendshapeCount = 0;

                    if (FaceTracker_TrackAll(pixelData, (uint)width, (uint)height, (uint)(width * 4)))
                    {
                        FaceTracker_GetFaceResult(out faceDetected, out yaw, out pitch, out roll, out x, out y, out blendshapeCount, _blendshapes, _blendshapes.Length);
                        FaceTracker_GetPoseResult(out poseDetected, out poseScore, _poseLandmarks, _poseVisibility, _posePresence);
                        
                        leftHandDetected = FaceTracker_GetHandLandmarks(true, _leftHandLandmarks);
                        rightHandDetected = FaceTracker_GetHandLandmarks(false, _rightHandLandmarks);
                        FaceTracker_GetFaceLandmarks(_faceLandmarks);
                    }

                    if (++_handLogCounter % 30 == 0) Log($"Tracking: success=True face={faceDetected} pose={poseDetected} left={leftHandDetected} right={rightHandDetected}");

                    Interlocked.Increment(ref _trackCallCount);
                    if (_trackFpsTimer.ElapsedMilliseconds >= 1000) {
                        Log($"Tracking FPS={_trackCallCount * 1000.0 / _trackFpsTimer.ElapsedMilliseconds:F1}");
                        _trackCallCount = 0; _trackFpsTimer.Restart();
                    }

                    _ = DispatcherQueue.TryEnqueue(() =>
                    {
                        FaceDetectedTextBlock.Text = $"Detected: {(faceDetected ? "Yes" : "No")}";
                        if (faceDetected) {
                            FaceYawTextBlock.Text = $"Yaw: {yaw:F2}"; FacePitchTextBlock.Text = $"Pitch: {pitch:F2}"; FaceRollTextBlock.Text = $"Roll: {roll:F2}";
                            FaceCenterTextBlock.Text = $"Center: ({x:F2}, {y:F2})"; BlendshapeCountTextBlock.Text = $"Blendshapes: {blendshapeCount}";
                        }
                        PoseDetectedTextBlock.Text = $"Detected: {(poseDetected ? "Yes" : "No")}";
                        PoseScoreTextBlock.Text = $"Score: {poseScore:F2}";
                        
                        UpdateOverlay(faceDetected, poseDetected, leftHandDetected, rightHandDetected);

                        LeftHandDetectedTextBlock.Text = $"Left: {(leftHandDetected ? "Yes" : "No")}";
                        RightHandDetectedTextBlock.Text = $"Right: {(rightHandDetected ? "Yes" : "No")}";
                    });
                }
                catch (Exception ex) { Log($"FaceTracker error: {ex.Message}"); }
                finally { Interlocked.Exchange(ref _isTracking, 0); }
            });
        }
        catch (Exception ex) { Log($"Frame processing error: {ex.Message}"); Interlocked.Exchange(ref _isTracking, 0); }
    }

    private async void OnClosed(object sender, WindowEventArgs args)
    {
        await StopPreviewAsync();
        FaceTracker_Shutdown();
    }

    private async Task StopPreviewAsync()
    {
        if (_frameReader is not null) {
            await _frameReader.StopAsync();
            _frameReader.FrameArrived -= FrameReader_FrameArrived;
            _frameReader.Dispose();
            _frameReader = null;
        }

        if (_mediaPlayer is not null)
        {
            try
            {
                _mediaPlayer.Pause();
            }
            catch
            {
            }
            _mediaPlayer.MediaFailed -= MediaPlayer_MediaFailed;
            PreviewElement.SetMediaPlayer(null);
            _mediaPlayer.Dispose();
            _mediaPlayer = null;
        }

        if (_mediaCapture is not null)
        {
            try
            {
                await _mediaCapture.StopPreviewAsync();
            }
            catch
            {
            }
            _mediaCapture.Dispose();
            _mediaCapture = null;
        }

        PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
    }

    private void MediaPlayer_MediaFailed(MediaPlayer sender, MediaPlayerFailedEventArgs args)
    {
        _ = DispatcherQueue.TryEnqueue(() =>
        {
            StatusTextBlock.Text = "Camera preview failed.";
            PreviewStateTextBlock.Text = $"Preview: {args.ErrorMessage}";
            PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
        });
    }

    private static MediaFrameFormat? SelectHighFpsFormat(MediaFrameSource source)
    {
        var formats = source.SupportedFormats
            .Where(f => f.VideoFormat is not null)
            .ToList();
        if (formats.Count == 0)
        {
            return null;
        }

        // Prefer Full HD (1920x1080) and fps near 30.
        // Fallback: choose format closest to 30 fps with larger resolution.
        var target = formats
            .Select(f => new
            {
                Format = f,
                Fps = GetFps(f),
                Width = (int)f.VideoFormat!.Width,
                Height = (int)f.VideoFormat.Height,
                Pixels = (long)f.VideoFormat.Width * f.VideoFormat.Height
            })
            .OrderByDescending(x => x.Width == 1920 && x.Height == 1080 ? 1 : 0)
            .ThenBy(x => Math.Abs(x.Fps - 30.0))
            .ThenByDescending(x => x.Pixels)
            .FirstOrDefault();

        return target?.Format;
    }

    private static double GetFps(MediaFrameFormat format)
    {
        var rate = format.FrameRate;
        if (rate is null || rate.Denominator == 0)
        {
            return 0;
        }

        return (double)rate.Numerator / rate.Denominator;
    }

    private static string DescribeFormat(MediaFrameFormat? format)
    {
        if (format?.VideoFormat is null)
        {
            return "(unknown)";
        }

        var fps = GetFps(format);
        return $"{format.VideoFormat.Width}x{format.VideoFormat.Height} {format.Subtype} {fps:F1}fps";
    }
}
