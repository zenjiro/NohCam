using Microsoft.UI.Xaml;
using Windows.Devices.Enumeration;
using Windows.Media.Capture;
using Windows.Media.Capture.Frames;
using Windows.Media.Playback;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Graphics.Imaging;
using System.IO;
using Windows.Storage.Streams;
using System.Threading;
using Windows.Media.MediaProperties;

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
    private static extern bool FaceTracker_Track(
        [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.U1)] byte[] pixels,
        uint width,
        uint height,
        uint stride,
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
    private static extern bool FaceTracker_TrackHands(
        [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.U1)] byte[] pixels,
        uint width,
        uint height,
        uint stride,
        out bool leftDetected,
        out float leftWristPitch,
        out float leftWristYaw,
        out float leftWristRoll,
        out bool rightDetected,
        out float rightWristPitch,
        out float rightWristYaw,
        out float rightWristRoll);

    private readonly float[] _blendshapes = new float[128];

    public MainWindow()
    {
        InitializeComponent();
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
        lock (_logLock) {
            _logWriter?.WriteLine($"[{DateTime.Now:HH:mm:ss.fff}] {msg}");
        }
        System.Diagnostics.Debug.WriteLine(msg);
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
                    bool detected = false;
                    float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0;
                    int blendshapeCount = 0;
                    bool leftDetected = false, rightDetected = false;
                    float leftWristPitch = 0, leftWristYaw = 0, leftWristRoll = 0;
                    float rightWristPitch = 0, rightWristYaw = 0, rightWristRoll = 0;

                    bool success = FaceTracker_Track(
                        pixelData,
                        (uint)width,
                        (uint)height,
                        (uint)(width * 4),
                        out detected,
                        out yaw,
                        out pitch,
                        out roll,
                        out x,
                        out y,
                        out blendshapeCount,
                        _blendshapes,
                        _blendshapes.Length);

                    bool handSuccess = FaceTracker_TrackHands(
                        pixelData,
                        (uint)width,
                        (uint)height,
                        (uint)(width * 4),
                        out leftDetected,
                        out leftWristPitch,
                        out leftWristYaw,
                        out leftWristRoll,
                        out rightDetected,
                        out rightWristPitch,
                        out rightWristYaw,
                        out rightWristRoll);
                    if (++_handLogCounter % 30 == 0)
                    {
                        Log($"Hands: success={handSuccess} left={leftDetected} right={rightDetected} lw=({leftWristPitch:F1},{leftWristYaw:F1},{leftWristRoll:F1}) rw=({rightWristPitch:F1},{rightWristYaw:F1},{rightWristRoll:F1})");
                    }
                    sw.Stop();
                    Interlocked.Increment(ref _trackCallCount);
                    if (sw.ElapsedMilliseconds > 300)
                    {
                        Log($"FaceTracker_Track took {sw.ElapsedMilliseconds}ms");
                    }
                    if (_trackFpsTimer.ElapsedMilliseconds >= 1000)
                    {
                        var elapsedMs = _trackFpsTimer.ElapsedMilliseconds;
                        var count = Interlocked.Exchange(ref _trackCallCount, 0);
                        var tps = elapsedMs > 0 ? count * 1000.0 / elapsedMs : 0;
                        Log($"Tracking FPS={tps:F1}");
                        _trackFpsTimer.Restart();
                    }

                    if (success || handSuccess)
                    {
                        _ = DispatcherQueue.TryEnqueue(() =>
                        {
                            FaceDetectedTextBlock.Text = $"Detected: {(detected ? "Yes" : "No")}";
                            if (detected)
                            {
                                FaceYawTextBlock.Text = $"Yaw: {yaw:F2}";
                                FacePitchTextBlock.Text = $"Pitch: {pitch:F2}";
                                FaceRollTextBlock.Text = $"Roll: {roll:F2}";
                                FaceCenterTextBlock.Text = $"Center: ({x:F2}, {y:F2})";
                                BlendshapeCountTextBlock.Text = $"Blendshapes: {blendshapeCount}";
                            }

                            LeftHandDetectedTextBlock.Text = $"Left: {(leftDetected ? "Yes" : "No")}";
                            LeftWristTextBlock.Text = $"Left Wrist: ({leftWristPitch:F1}, {leftWristYaw:F1}, {leftWristRoll:F1})";
                            RightHandDetectedTextBlock.Text = $"Right: {(rightDetected ? "Yes" : "No")}";
                            RightWristTextBlock.Text = $"Right Wrist: ({rightWristPitch:F1}, {rightWristYaw:F1}, {rightWristRoll:F1})";
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
