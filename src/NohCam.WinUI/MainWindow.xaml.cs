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

    private readonly float[] _poseLandmarks = new float[33 * 3];
    private readonly float[] _poseVisibility = new float[33];
    private readonly float[] _posePresence = new float[33];

    private readonly float[] _blendshapes = new float[128];

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

    private void FrameReader_FrameArrived(MediaFrameReader sender, MediaFrameArrivedEventArgs args)
    {
        _debugFrameCount++;
        _fpsFrameCount++;

        // Log every 30 frames
        if (_debugFrameCount % 30 == 0) {
            var elapsed = _fpsTimer.ElapsedMilliseconds;
            double fps = elapsed > 0 ? _fpsFrameCount * 1000.0 / elapsed : 0;
            Log($"Frame #{_debugFrameCount}: FPS={fps:F1}");
        }

        // Track only on interval and never overlap native tracking calls.
        if (_trackTimer.ElapsedMilliseconds < _trackIntervalMs) {
            return;
        }

        if (Interlocked.CompareExchange(ref _isTracking, 1, 0) != 0) {
            return;
        }

        _trackTimer.Restart();

        try
        {
            using var frame = sender.TryAcquireLatestFrame();
            if (frame?.VideoMediaFrame?.SoftwareBitmap is null) {
                return;
            }

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

            // Run native tracking off-thread to keep frame callbacks responsive.
            _ = Task.Run(() =>
            {
                try
                {
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    bool faceDetected = false;
                    float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0;
                    int blendshapeCount = 0;

                    bool poseDetected = false;
                    float poseScore = 0;

                    bool leftHandDetected = false, rightHandDetected = false;
                    float leftWristX = 0, leftWristY = 0, leftWristZ = 0;
                    float rightWristX = 0, rightWristY = 0, rightWristZ = 0;

                    bool trackAllSuccess = FaceTracker_TrackAll(
                        pixelData,
                        (uint)width,
                        (uint)height,
                        (uint)(width * 4));

                    if (trackAllSuccess)
                    {
                        FaceTracker_GetFaceResult(
                            out faceDetected,
                            out yaw,
                            out pitch,
                            out roll,
                            out x,
                            out y,
                            out blendshapeCount,
                            _blendshapes,
                            _blendshapes.Length);

                        FaceTracker_GetPoseResult(
                            out poseDetected,
                            out poseScore,
                            _poseLandmarks,
                            _poseVisibility,
                            _posePresence);

                        FaceTracker_GetHandResult(
                            out leftHandDetected,
                            out leftWristX,
                            out leftWristY,
                            out leftWristZ,
                            out rightHandDetected,
                            out rightWristX,
                            out rightWristY,
                            out rightWristZ);
                    }

                    if (++_handLogCounter % 30 == 0)
                    {
                        Log($"Tracking: success={trackAllSuccess} face={faceDetected} pose={poseDetected} left={leftHandDetected} right={rightHandDetected}");
                    }
                    sw.Stop();
                    Interlocked.Increment(ref _trackCallCount);

                    if (_trackFpsTimer.ElapsedMilliseconds >= 1000)
                    {
                        var elapsedMs = _trackFpsTimer.ElapsedMilliseconds;
                        var count = Interlocked.Exchange(ref _trackCallCount, 0);
                        var tps = elapsedMs > 0 ? count * 1000.0 / elapsedMs : 0;
                        Log($"Tracking FPS={tps:F1}");
                        _trackFpsTimer.Restart();
                    }

                    if (trackAllSuccess)
                    {
                        _ = DispatcherQueue.TryEnqueue(() =>
                        {
                            FaceDetectedTextBlock.Text = $"Detected: {(faceDetected ? "Yes" : "No")}";
                            if (faceDetected)
                            {
                                FaceYawTextBlock.Text = $"Yaw: {yaw:F2}";
                                FacePitchTextBlock.Text = $"Pitch: {pitch:F2}";
                                FaceRollTextBlock.Text = $"Roll: {roll:F2}";
                                FaceCenterTextBlock.Text = $"Center: ({x:F2}, {y:F2})";
                                BlendshapeCountTextBlock.Text = $"Blendshapes: {blendshapeCount}";
                            }

                            PoseDetectedTextBlock.Text = $"Detected: {(poseDetected ? "Yes" : "No")}";
                            PoseScoreTextBlock.Text = $"Score: {poseScore:F2}";
                            if (poseDetected)
                            {
                                // Landmark 15 is Left Wrist, 16 is Right Wrist
                                float lwX = _poseLandmarks[15 * 3 + 0];
                                float lwY = _poseLandmarks[15 * 3 + 1];
                                float rwX = _poseLandmarks[16 * 3 + 0];
                                float rwY = _poseLandmarks[16 * 3 + 1];
                                PoseWristsTextBlock.Text = $"Wrists (L/R): ({lwX:F2}, {lwY:F2}) / ({rwX:F2}, {rwY:F2})";
                            }

                            LeftHandDetectedTextBlock.Text = $"Left: {(leftHandDetected ? "Yes" : "No")}";
                            LeftWristTextBlock.Text = $"Left Wrist: ({leftWristX:F2}, {leftWristY:F2}, {leftWristZ:F2})";
                            RightHandDetectedTextBlock.Text = $"Right: {(rightHandDetected ? "Yes" : "No")}";
                            RightWristTextBlock.Text = $"Right Wrist: ({rightWristX:F2}, {rightWristY:F2}, {rightWristZ:F2})";
                        });
                    }
                }
                catch (Exception ex)
                {
                    Log($"FaceTracker error: {ex.Message}");
                }
                finally
                {
                    Interlocked.Exchange(ref _isTracking, 0);
                }
            });
        }
        catch (Exception ex)
        {
            Log($"Frame processing error: {ex.Message}");
            Interlocked.Exchange(ref _isTracking, 0);
        }
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
