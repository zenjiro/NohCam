using Microsoft.UI.Xaml;
using Windows.Devices.Enumeration;
using Windows.Media.Capture;
using Windows.Media.Capture.Frames;
using Windows.Media.Core;
using Windows.Media.Playback;
using Windows.Media.MediaProperties;
using System.Runtime.InteropServices;
using Windows.Graphics.Imaging;

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

    [DllImport("nohcam_bridge.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern bool FaceTracker_Track(
        IntPtr pixels,
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
        float[] blendshapes);

    private readonly float[] _blendshapes = new float[128];

    public MainWindow()
    {
        InitializeComponent();
        Activated += OnActivated;
        Closed += OnClosed;
    }

    private async void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        Activated -= OnActivated;

        StatusTextBlock.Text = "Initializing tracker...";

        try {
            StatusTextBlock.Text = "Calling Initialize...";
            bool success = FaceTracker_Initialize();
            var errorBuilder = new System.Text.StringBuilder(1024);
            FaceTracker_GetInitError(errorBuilder, 1024);
            var error = errorBuilder.ToString();
            StatusTextBlock.Text = $"Tracker Init: {(success ? "OK" : "FAILED")} {error}";
        } catch (Exception ex) {
            StatusTextBlock.Text = $"Tracker Init Exception: {ex}";
        }

        await StartPreviewAsync();
    }

    private async void RestartPreview_Click(object sender, RoutedEventArgs e)
    {
        await StartPreviewAsync();
    }

    private async Task StartPreviewAsync()
    {
        StatusTextBlock.Text = "Opening camera...";
        PreviewStateTextBlock.Text = "Preview: initializing";

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
                return;
            }

            DeviceTextBlock.Text = $"Device: {firstDevice.Name}";

            _mediaCapture = new MediaCapture();
            await _mediaCapture.InitializeAsync(new MediaCaptureInitializationSettings
            {
                VideoDeviceId = firstDevice.Id,
                StreamingCaptureMode = StreamingCaptureMode.Video,
                SharingMode = MediaCaptureSharingMode.ExclusiveControl,
                MemoryPreference = MediaCaptureMemoryPreference.Auto
            });

            var frameSource = _mediaCapture.FrameSources.Values.FirstOrDefault(source =>
                source.Info.SourceKind == MediaFrameSourceKind.Color &&
                (source.Info.MediaStreamType == MediaStreamType.VideoPreview ||
                 source.Info.MediaStreamType == MediaStreamType.VideoRecord));

            if (frameSource is null)
            {
                throw new InvalidOperationException("No color frame source is available.");
            }

            var preferredFormat = frameSource.SupportedFormats.FirstOrDefault(format => format.Subtype == MediaEncodingSubtypes.Bgra8);
            if (preferredFormat is not null)
            {
                await frameSource.SetFormatAsync(preferredFormat);
            }

            _mediaPlayer = new MediaPlayer
            {
                RealTimePlayback = true,
                AutoPlay = false,
                Source = MediaSource.CreateFromMediaFrameSource(frameSource)
            };
            _mediaPlayer.MediaFailed += MediaPlayer_MediaFailed;
            PreviewElement.SetMediaPlayer(_mediaPlayer);
            PreviewPlaceholderTextBlock.Visibility = Visibility.Collapsed;
            _mediaPlayer.Play();

            _frameReader = await _mediaCapture.CreateFrameReaderAsync(frameSource, MediaEncodingSubtypes.Bgra8);
            _frameReader.FrameArrived += FrameReader_FrameArrived;
            await _frameReader.StartAsync();

            StatusTextBlock.Text = "Camera preview is running.";
            PreviewStateTextBlock.Text = "Preview: active (Tracking ON)";
        }
        catch (Exception ex)
        {
            StatusTextBlock.Text = "Camera preview failed.";
            PreviewStateTextBlock.Text = $"Preview: {ex.Message}";
            PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
        }
    }

    private int _frameCount = 0;
    private void FrameReader_FrameArrived(MediaFrameReader sender, MediaFrameArrivedEventArgs args)
    {
        using var frame = sender.TryAcquireLatestFrame();
        if (frame?.VideoMediaFrame?.SoftwareBitmap is null) return;

        using var bitmap = SoftwareBitmap.Convert(frame.VideoMediaFrame.SoftwareBitmap, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore);
        
        unsafe {
            using var buffer = bitmap.LockBuffer(BitmapBufferAccessMode.Read);
            using var reference = buffer.CreateReference();
            ((IMemoryBufferByteAccess)reference).GetBuffer(out byte* data, out uint capacity);

            bool detected = false;
            float yaw = 0, pitch = 0, roll = 0, x = 0, y = 0;
            int blendshapeCount = 0;

            bool success = FaceTracker_Track(
                (IntPtr)data,
                (uint)bitmap.PixelWidth,
                (uint)bitmap.PixelHeight,
                (uint)(bitmap.PixelWidth * 4),
                out detected,
                out yaw,
                out pitch,
                out roll,
                out x,
                out y,
                out blendshapeCount,
                _blendshapes);

            if (success && _frameCount++ % 30 == 0) {
                _ = DispatcherQueue.TryEnqueue(() => {
                    FaceDetectedTextBlock.Text = $"Detected: {(detected ? "Yes" : "No")}";
                    if (detected) {
                        FaceYawTextBlock.Text = $"Yaw: {yaw:F2}";
                        FacePitchTextBlock.Text = $"Pitch: {pitch:F2}";
                        FaceRollTextBlock.Text = $"Roll: {roll:F2}";
                        FaceCenterTextBlock.Text = $"Center: ({x:F2}, {y:F2})";
                        BlendshapeCountTextBlock.Text = $"Blendshapes: {blendshapeCount}";
                    }
                });
            }
        }
    }

    [ComImport]
    [Guid("5B0D3235-4DB0-4D44-9101-9C8015E10746")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    unsafe interface IMemoryBufferByteAccess
    {
        void GetBuffer(out byte* buffer, out uint capacity);
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

        if (_mediaCapture is null)
        {
            return;
        }

        PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
        _mediaCapture.Dispose();
        _mediaCapture = null;
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
}
