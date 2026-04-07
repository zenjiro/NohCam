using Microsoft.UI.Xaml;
using Windows.Devices.Enumeration;
using Windows.Media.Capture;
using Windows.Media.Capture.Frames;
using Windows.Media.Core;
using Windows.Media.Playback;
using Windows.Media.MediaProperties;

namespace NohCam.WinUI;

public sealed partial class MainWindow : Window
{
    private MediaCapture? _mediaCapture;
    private MediaPlayer? _mediaPlayer;

    public MainWindow()
    {
        InitializeComponent();
        Activated += OnActivated;
        Closed += OnClosed;
    }

    private async void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        Activated -= OnActivated;
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

            StatusTextBlock.Text = "Camera preview is running.";
            PreviewStateTextBlock.Text = "Preview: active (640x360 / 10fps)";
        }
        catch (Exception ex)
        {
            StatusTextBlock.Text = "Camera preview failed.";
            PreviewStateTextBlock.Text = $"Preview: {ex.Message}";
            PreviewPlaceholderTextBlock.Visibility = Visibility.Visible;
        }
    }

    private async void OnClosed(object sender, WindowEventArgs args)
    {
        await StopPreviewAsync();
    }

    private async Task StopPreviewAsync()
    {
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

        try
        {
        }
        catch
        {
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
